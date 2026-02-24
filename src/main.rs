use h264_reader::annexb::AnnexBReader;
use h264_reader::nal::Nal;
use h264_reader::nal::RefNal;
use h264_reader::nal::UnitType;
use h264_reader::nal::sei::HeaderType;
use h264_reader::nal::sei::SeiMessage;
use h264_reader::nal::sei::SeiReader;
use h264_reader::push::NalInterest;
use memchr::memmem;
use rand::prelude::*;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Write;
use std::path::Path;

struct OFMDdata {
    frame_rate: u8,
    total_frames: usize,
    num_of_planes: usize,
    valid_planes: Vec<bool>,
    planes: Vec<Vec<u8>>,
}

fn main() {
    let path = std::env::args().nth(1).expect("No path given");
    let out_directory = std::env::args().nth(2).expect("No out path given");
    let mut ofmd_array: Vec<Vec<u8>> = Vec::new();

    get_ofmds_in_file(path, &mut ofmd_array);

    let mut ofmd_data = parse_ofmds(&mut ofmd_array);
    verify_planes(&mut ofmd_data);

    create_ofs_files(&ofmd_data, out_directory, false);
}

fn verify_planes(ofmd_data: &mut OFMDdata) {
    ofmd_data.valid_planes = vec![false; ofmd_data.num_of_planes];
    for x in 0..ofmd_data.num_of_planes {
        let mut there_are_planes: bool = false;
        for y in 0..ofmd_data.total_frames {
            if ofmd_data.planes[x][y] != 0x80 {
                there_are_planes = true;
                ofmd_data.valid_planes[x] = true;
            }
        }

        if there_are_planes {
            println!();
            println!("3D-Plane #{}", x);
            parse_depths(ofmd_data, x);
        } else {
            println!();
            println!("3D-Plane #{} is empty.", x);
        }
    }
}

fn compare_depths(ofmd_data: &OFMDdata, plane_num: usize) {
    let mut message_string = "Identical Planes:".to_string();
    let mut same_plane = false;

    for x in 0..ofmd_data.num_of_planes {
        if ofmd_data.planes[plane_num] == ofmd_data.planes[x] && x != plane_num {
            message_string += &format!(" {}", x).to_string();
            same_plane = true;
        }
    }

    if same_plane {
        println!("{}", message_string);
    } else {
        println!("{}", message_string + " None");
    }
}

fn parse_depths(ofmd_data: &OFMDdata, plane_num: usize) {
    let mut minval = 128;
    let mut maxval = -128;
    let mut total = 0;
    let mut undefined = 0;
    let mut firstframe = -1;
    let mut lastframe = -1;
    let mut lastval = 0;
    let mut byte: i32;
    let mut cuts = 0;

    for i in 0..ofmd_data.total_frames {
        byte = ofmd_data.planes[plane_num][i] as i32;
        if byte != lastval {
            cuts += cuts;
            lastval = byte;
        }
        if byte == 128 {
            undefined += 1;
            continue;
        } else {
            lastframe = i as i32;
            if firstframe == -1 {
                firstframe = i as i32;
            }
        }

        if byte > 128 {
            byte = 128 - byte;
        }

        if byte < minval {
            minval = byte;
        }
        if byte > maxval {
            maxval = byte;
        }
        total += byte;
    }

    let average: f32 = total as f32 / (ofmd_data.total_frames as f32 - undefined as f32);
    println!("NumFrames: {}", ofmd_data.total_frames);
    println!("Minimum depth: {}", minval);
    println!("Maximum depth: {}", maxval);
    println!("Average depth: {:.3}", average);
    println!("Number of changes of depth value: {}", cuts);
    println!("First frame with defined depth: {}", firstframe);
    println!("Last frame with defined depth: {}", lastframe);
    compare_depths(ofmd_data, plane_num);
    if minval == maxval {
        println!(
            "*** Warning This 3D-Plane has a fixed depth of {}! ***",
            minval,
        );
    }
}

fn parse_ofmds(ofmd_array: &mut Vec<Vec<u8>>) -> Box<OFMDdata> {
    let mut ofmd_data = Box::<OFMDdata>::new(OFMDdata {
        frame_rate: ofmd_array[0][4] & 15,
        total_frames: 0,
        num_of_planes: ofmd_array[0][10] as usize & 0x7F,
        valid_planes: vec![],
        planes: vec![vec![]],
    });

    for x in ofmd_array.iter() {
        ofmd_data.total_frames += x[11] as usize & 127;
    }

    // Pre-allocate vectors for performance.
    ofmd_data.planes = vec![vec![0; ofmd_data.total_frames]; ofmd_data.num_of_planes];

    let mut counter: usize;
    let mut total_frames: usize = 0;
    for ofmd in ofmd_array {
        let frame_count: usize = ofmd[11] as usize & 127;
        for plane in 0..ofmd_data.num_of_planes {
            counter = total_frames;
            let start = 14 + (plane * frame_count);
            let end = 14 + (plane * frame_count) + frame_count;
            for y in ofmd.iter().take(end).skip(start) {
                ofmd_data.planes[plane][counter] = *y;
                counter += 1;
            }
        }
        total_frames += frame_count
    }
    ofmd_data
}

fn get_ofmd_from_sei<'a>(sei: &'a SeiMessage, buf: &'a mut Vec<u8>) -> bool {
    let mut payload_string = sei.payload;
    let ofmd_match = memmem::Finder::new("OFMD")
        .find(payload_string)
        .unwrap_or_default();

    if ofmd_match == 0 {
        return false;
    }

    payload_string = &payload_string[ofmd_match..];

    // Do a crude check by validating the frame rate value.
    let frame_rate = payload_string[4] & 15;
    if !(1..=7).contains(&frame_rate) && frame_rate != 5 {
        return false;
    }

    *buf = payload_string.to_vec();

    true
}

fn get_ofmds_in_file(path: String, ofmd_array: &mut Vec<Vec<u8>>) {
    let mut reader = AnnexBReader::accumulate(|nal: RefNal<'_>| {
        if !nal.is_complete() {
            return NalInterest::Buffer;
        }

        let nal_header = nal.header().unwrap();
        let nal_unit_type = nal_header.nal_unit_type();

        if nal_unit_type == UnitType::SEI {
            let mut scratch = vec![];
            let mut reader = SeiReader::from_rbsp_bytes(nal.rbsp_bytes(), &mut scratch);
            loop {
                match reader.next() {
                    Ok(Some(sei)) => {
                        if sei.payload_type == HeaderType::MvcScalableNesting {
                            let mut buf: Vec<u8> = vec![];

                            if get_ofmd_from_sei(&sei, &mut buf) {
                                ofmd_array.push(buf)
                            }
                        }
                    }
                    Ok(None) => break,
                    Err(e) => {
                        println!("{:?}", e);
                    }
                }
            }
        }
        NalInterest::Ignore
    });

    let mut use_stdin = false;
    if path == "-" {
        use_stdin = true;
    }

    let mut file_size = 0;
    let mut buf_reader: Box<dyn BufRead> = Box::new(std::io::stdin().lock());
    if !use_stdin {
        let file = File::open(path).expect("can't open file");
        file_size = file.metadata().expect("can't get file metadata").len();
        buf_reader = Box::new(BufReader::new(file));
    }

    let mut file_position = 0;
    let mut progress = 0;
    let mut last_progress = progress;
    loop {
        let buf = buf_reader.fill_buf().expect("Fill file buffer");
        let buf_len = buf.len();

        if buf.is_empty() {
            break;
        }

        reader.push(buf);
        if !use_stdin {
            file_position += buf_len;
            progress = ((file_position as f32 / file_size as f32) * 100.0) as i32;
        }

        buf_reader.consume(buf_len);

        if progress != last_progress {
            println!("Progress: {}%", progress);
            last_progress = progress;
        }
    }
    reader.reset();
}

fn create_ofs_files(ofmd_data: &OFMDdata, out_directory: String, drop_frame: bool) {
    // Structure of an OFS file.
    //
    // Pulled from BD3D2MK3D's 3DPlanes2OFS sources. (thanks, r0lZ!)
    //
    // filename.ofs {
    // 	signature               8*8  (8) : 0x894f46530d0a1a0a
    // 	version                 8*4  (4) : "0100" coded according to ISO 646 : 0x30313030
    // 	guid                   8*16 (16) : random number based on the 3D-Plane number?
    // 	frame_rate                4  (1) : 1 ?  -> 0x10
    // 	drop_frame_flag           4   '  : 0
    // 	number_of_rolls           8  (1) : 1 ?  -> 0x01
    // 	reserved                8*2  (2) : 0x00 -> 0x0000
    // 	for(i=0; i<number_of_rolls; i++){
    // 		marker_bits         1*8  (1) : '00000000' -> 0x00
    // 		start_timecode      4*8  (4) : 0x00000000 ?
    // 		number_of_frames     32  (4) : [string length $plane]
    // 		# Total bytes in the header: 41
    // 		# Here comes the 3D-Plane itself, one byte per frame.
    // 		for(j=0; j<number_of_frames; j++){
    // 			offset_direction  1  (1) : "sign" of the 3D depth offset
    // 			offset_value      7   '  : The 3D offset value
    // 		}
    // 	}
    // }
    //
    let signature: [u8; 8] = [0x89, 0x4f, 0x46, 0x53, 0x0d, 0x0a, 0x1a, 0x0a];
    let version: [u8; 4] = [0x30, 0x31, 0x30, 0x30];
    let mut guid: [u8; 16] = std::array::from_fn(|_| rand::rng().random::<u8>());
    let rolls_and_reserved: [u8; 4] = [0x01, 0x00, 0x00, 0x00];
    let timecode: [u8; 4] = [0x00, 0x00, 0x00, 0x00];
    let mut frame_array: [u8; 4] = [0x00, 0x00, 0x00, 0x00];

    let path = Path::new(&out_directory);
    if !path.try_exists().expect("path exists") {
        std::fs::create_dir(path).expect("create dir");
    }

    let mut buffer: Vec<u8> = vec![];
    buffer.extend_from_slice(&signature);
    buffer.extend_from_slice(&version);

    // Store the framecount in 4 bytes.
    frame_array[3] = (ofmd_data.total_frames % 256) as u8;
    frame_array[2] = ((ofmd_data.total_frames >> 8) % 256) as u8;
    frame_array[1] = ((ofmd_data.total_frames >> 16) % 256) as u8;
    frame_array[0] = ((ofmd_data.total_frames >> 24) % 256) as u8;

    let frame_rate = (ofmd_data.frame_rate * 16) + drop_frame as u8;

    for plane in 0..ofmd_data.num_of_planes {
        if ofmd_data.valid_planes[plane] {
            guid[15] = plane as u8;

            buffer.extend_from_slice(&guid);
            buffer.push(frame_rate);

            buffer.extend_from_slice(&rolls_and_reserved);
            buffer.extend_from_slice(&timecode);
            buffer.extend_from_slice(&frame_array);

            for x in 0..ofmd_data.total_frames {
                buffer.push(ofmd_data.planes[plane][x]);
            }

            let out_path = path.join(format!("3D-Planes-{:02}.ofs", plane));
            let mut out_file = std::fs::File::create(out_path).expect("create file");

            out_file.write_all(&buffer).expect("write file");
        }
    }
}
