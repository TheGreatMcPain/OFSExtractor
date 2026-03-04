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

#[derive(Default, Clone)]
pub struct OFMDPlane {
    pub id: usize,
    pub frame_rate: u8,
    pub total_frames: usize,
    pub valid: bool,
    pub depths: Vec<u8>,
    identical_planes: Vec<u8>,
}

impl OFMDPlane {
    pub fn get_planes(path: &str) -> Result<Vec<OFMDPlane>, std::io::Error> {
        let mut ofmd_planes: Vec<OFMDPlane> = vec![];

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
                                    parse_ofmd(&buf, &mut ofmd_planes);
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

        for plane_num in 0..ofmd_planes.len() {
            let mut plane = ofmd_planes[plane_num].clone();

            // If a plane has no depth (all 0x80), it's not valid.
            if !plane.depths.iter().any(|&x| x != 0x80) {
                plane.valid = false;
            }

            // Check for planes with Identical depth.
            for x in ofmd_planes.iter() {
                if plane.depths == x.depths && plane.id != x.id {
                    plane.identical_planes.push(x.id as u8);
                }
            }

            ofmd_planes[plane_num] = plane;
        }

        Ok(ofmd_planes)
    }
}

impl std::fmt::Display for OFMDPlane {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f)?;

        if self.valid {
            writeln!(f, "3D-Plane #{:02}", self.id)?;
            write!(f, "{}", parse_depths(self))?;
        } else {
            write!(f, "3D-Plane #{:02} is empty.", self.id)?;
        }

        Ok(())
    }
}

pub fn parse_depths(ofmd_plane: &OFMDPlane) -> String {
    let mut minval = 128;
    let mut maxval = -128;
    let mut total = 0;
    let mut undefined = 0;
    let mut firstframe = -1;
    let mut lastframe = -1;
    let mut lastval = 0;
    let mut cuts = 0;

    let mut output: String = String::new();

    for i in 0..ofmd_plane.total_frames {
        let mut byte = ofmd_plane.depths[i] as i32;
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

    writeln!(output, "NumFrames: {}", ofmd_plane.total_frames).unwrap();
    writeln!(output, "Minimum depth: {}", minval).unwrap();
    writeln!(output, "Maximum depth: {}", maxval).unwrap();
    writeln!(
        output,
        "Average depth: {:.2}",
        total as f32 / (ofmd_plane.total_frames as f32 - undefined as f32)
    )
    .unwrap();
    writeln!(output, "Number of changes of depth value: {}", cuts).unwrap();
    writeln!(output, "First frame with defined depth: {}", firstframe).unwrap();
    writeln!(output, "Last frame with defined depth: {}", lastframe).unwrap();
    if ofmd_plane.identical_planes.is_empty() {
        write!(output, "Identical Planes: None").unwrap();
    } else {
        write!(output, "Identical Planes:").unwrap();
        for x in ofmd_plane.identical_planes.clone() {
            write!(output, " {}", x).unwrap();
        }
    }
    if minval == maxval {
        write!(
            output,
            "\n*** Warning This 3D-Plane has a fixed depth of {}! ***",
            minval,
        )
        .unwrap();
    }

    output
}

fn parse_ofmd(ofmd: &[u8], ofmd_planes: &mut Vec<OFMDPlane>) {
    let num_of_planes = ofmd[10] as usize & 0x7F;
    let frame_count = ofmd[11] as usize & 127;

    if ofmd_planes.is_empty() {
        for x in 0..num_of_planes {
            ofmd_planes.push(OFMDPlane {
                id: x,
                frame_rate: ofmd[4] & 15,
                total_frames: 0,
                valid: true,
                depths: vec![],
                identical_planes: vec![],
            });
        }
    }

    for plane in ofmd_planes {
        plane.depths.extend(
            ofmd.iter()
                .take(14 + (plane.id * frame_count) + frame_count)
                .skip(14 + (plane.id * frame_count)),
        );

        plane.total_frames += frame_count;
    }
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
