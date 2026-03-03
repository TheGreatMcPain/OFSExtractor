use h264_reader::annexb::AnnexBReader;
use h264_reader::nal::Nal;
use h264_reader::nal::RefNal;
use h264_reader::nal::UnitType;
use h264_reader::nal::sei::HeaderType;
use h264_reader::nal::sei::SeiMessage;
use h264_reader::nal::sei::SeiReader;
use h264_reader::push::NalInterest;
use memchr::memmem;
use std::fmt::Write;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;

#[derive(Default)]
pub struct OFMDdata {
    pub frame_rate: u8,
    pub total_frames: usize,
    pub num_of_planes: usize,
    pub planes: Vec<Vec<u8>>,
}

impl OFMDdata {
    pub fn new(path: &str) -> Result<OFMDdata, std::io::Error> {
        let mut ofmd_data = OFMDdata::default();

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
                                    parse_ofmd(&buf, &mut ofmd_data);
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
            let file = File::open(path)?;
            file_size = file.metadata()?.len();
            buf_reader = Box::new(BufReader::new(file));
        }

        let mut file_position = 0;
        let mut progress = 0;
        let mut last_progress = progress;
        loop {
            let buf = buf_reader.fill_buf()?;
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

        // If a plane has no depth (all 0x80), clear it.
        for plane in &mut ofmd_data.planes {
            if !plane.iter().any(|&x| x != 0x80) {
                plane.clear();
            }
        }

        Ok(ofmd_data)
    }
}

impl std::fmt::Display for OFMDdata {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f)?;
        for x in 0..self.num_of_planes {
            let plane = &self.planes[x];

            if plane.is_empty() {
                writeln!(f, "3D-Plane #{:02} is empty.", x)?;
            } else {
                writeln!(f, "3D-Plane #{:02}", x)?;
                writeln!(f, "{}", parse_depths(self, x))?;
            }
        }

        Ok(())
    }
}

pub fn compare_depths(ofmd_data: &OFMDdata, plane_num: usize) -> String {
    let mut message_string = String::new();
    let mut identical = vec![];

    for x in 0..ofmd_data.num_of_planes {
        if ofmd_data.planes[plane_num] == ofmd_data.planes[x] && x != plane_num {
            identical.push(x);
        }
    }

    if identical.is_empty() {
        writeln!(message_string, "Identical Planes: None").unwrap();
    } else {
        write!(message_string, "Identical Planes:").unwrap();
        for x in identical {
            write!(message_string, " {}", x).unwrap();
        }

        writeln!(message_string).unwrap();
    }

    message_string
}

pub fn parse_depths(ofmd_data: &OFMDdata, plane_num: usize) -> String {
    let mut minval = 128;
    let mut maxval = -128;
    let mut total = 0;
    let mut undefined = 0;
    let mut firstframe = -1;
    let mut lastframe = -1;
    let mut lastval = 0;
    let mut cuts = 0;

    let mut output: String = String::new();

    for i in 0..ofmd_data.total_frames {
        let mut byte = ofmd_data.planes[plane_num][i] as i32;
        if byte != lastval {
            cuts += 1;
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

    writeln!(output, "NumFrames: {}", ofmd_data.total_frames).unwrap();
    writeln!(output, "Minimum depth: {}", minval).unwrap();
    writeln!(output, "Maximum depth: {}", maxval).unwrap();
    writeln!(
        output,
        "Average depth: {:.2}",
        total as f32 / (ofmd_data.total_frames as f32 - undefined as f32)
    )
    .unwrap();
    writeln!(output, "Number of changes of depth value: {}", cuts).unwrap();
    writeln!(output, "First frame with defined depth: {}", firstframe).unwrap();
    writeln!(output, "Last frame with defined depth: {}", lastframe).unwrap();
    write!(output, "{}", compare_depths(ofmd_data, plane_num)).unwrap();
    if minval == maxval {
        writeln!(
            output,
            "*** Warning This 3D-Plane has a fixed depth of {}! ***",
            minval,
        )
        .unwrap();
    }

    output
}

fn parse_ofmd(ofmd: &[u8], ofmd_data: &mut OFMDdata) {
    if ofmd_data.planes.is_empty() {
        ofmd_data.frame_rate = ofmd[4] & 15;
        ofmd_data.num_of_planes = ofmd[10] as usize & 0x7F;
        ofmd_data.planes = vec![vec![]; ofmd_data.num_of_planes];
    }

    let frame_count = ofmd[11] as usize & 127;

    for plane in 0..ofmd_data.num_of_planes {
        ofmd_data.planes[plane].extend(
            ofmd.iter()
                .take(14 + (plane * frame_count) + frame_count)
                .skip(14 + (plane * frame_count)),
        );
    }
    ofmd_data.total_frames += frame_count;
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
