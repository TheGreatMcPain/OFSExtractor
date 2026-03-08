use h264_reader::annexb::AnnexBReader;
use h264_reader::nal::Nal;
use h264_reader::nal::RefNal;
use h264_reader::nal::UnitType;
use h264_reader::nal::sei::HeaderType;
use h264_reader::nal::sei::SeiMessage;
use h264_reader::nal::sei::SeiReader;
use h264_reader::push::NalInterest;
use itertools::Itertools;
use memchr::memmem;
use rand::RngExt;
use std::cell::RefCell;
use std::fs::File;
use std::io::BufReader;
use std::io::Write;
use std::path::Path;
use wincode::SchemaRead;
use wincode::SchemaWrite;

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
#[derive(SchemaWrite, SchemaRead, Clone)]
pub struct OffsetMetadataSequence {
    signature: [u8; 8],
    version: [u8; 4],
    guid: [u8; 16],
    frame_rate_and_drop_frame: u8,
    rolls_and_reserved: [u8; 4],
    timecode: [u8; 4],
    number_of_frames: u32,
    offsets: Vec<u8>,
}

impl OffsetMetadataSequence {
    pub fn new() -> OffsetMetadataSequence {
        OffsetMetadataSequence {
            signature: [0x89, 0x4f, 0x46, 0x53, 0x0d, 0x0a, 0x1a, 0x0a],
            version: [0x30, 0x31, 0x30, 0x30],
            frame_rate_and_drop_frame: 0,
            guid: std::array::from_fn(|_| rand::rng().random::<u8>()),
            rolls_and_reserved: [0; 4],
            timecode: [0; 4],
            number_of_frames: 0,
            offsets: vec![],
        }
    }

    pub fn get_from_h264(
        path: &str,
        drop_frame: bool,
    ) -> Result<Vec<OffsetMetadataSequence>, std::io::Error> {
        let mut ofs_planes = Vec::<OffsetMetadataSequence>::new();
        let reader_error = RefCell::new(Ok(()));

        let mut reader = AnnexBReader::accumulate(|nal: RefNal<'_>| {
            if !nal.is_complete() {
                return NalInterest::Buffer;
            }

            let nal_header = match nal.header() {
                Ok(header) => header,
                Err(e) => {
                    *reader_error.borrow_mut() = Err(e);
                    return NalInterest::Ignore;
                }
            };
            if nal_header.nal_unit_type() == UnitType::SEI {
                let mut scratch = vec![];
                let mut reader = SeiReader::from_rbsp_bytes(nal.rbsp_bytes(), &mut scratch);
                loop {
                    match reader.next() {
                        Ok(Some(sei)) => {
                            if sei.payload_type == HeaderType::MvcScalableNesting {
                                if memmem::Finder::new("OFMD")
                                    .find(sei.payload)
                                    .unwrap_or_default()
                                    == 0
                                {
                                    continue;
                                }

                                if ofs_planes.is_empty() {
                                    Self::init_ofs_planes(&mut ofs_planes, &sei, drop_frame);
                                }

                                for ofs_plane in ofs_planes.iter_mut() {
                                    ofs_plane.update_from_sei(&sei, ofs_plane.get_id());
                                }
                            }
                        }
                        Ok(None) => break,
                        Err(_) => {}
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
        let mut buf_reader: Box<dyn std::io::BufRead> = Box::new(std::io::stdin().lock());
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
            if reader_error.borrow().is_err() {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "Input data is invalid or corrupt.",
                ));
            }

            if !use_stdin {
                file_position += buf_len;
                progress = ((file_position as f32 / file_size as f32) * 100.0) as i32;
                if progress != last_progress {
                    println!("Progress: {}%", progress);
                    last_progress = progress;
                }
            }

            buf_reader.consume(buf_len);
        }
        reader.reset();
        println!();

        if ofs_planes.is_empty() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "No depth values found in file.",
            ));
        }

        for plane in ofs_planes.iter_mut() {
            if !plane.offsets.iter().any(|&x| x != 0x80) {
                plane.offsets.clear();
            }
        }

        Ok(ofs_planes)
    }

    fn init_ofs_planes(planes: &mut Vec<Self>, sei: &SeiMessage, drop_frame: bool) {
        let ofmd_match = memmem::Finder::new("OFMD")
            .find(sei.payload)
            .unwrap_or_default();

        assert!(ofmd_match != 0);

        let ofmd = &sei.payload[ofmd_match..];

        let num_of_planes = ofmd[10] as usize & 127;

        for x in 0..num_of_planes {
            let mut ofs = OffsetMetadataSequence::new();
            ofs.frame_rate_and_drop_frame = ((ofmd[4] & 15) << 4) + drop_frame as u8;
            ofs.guid[15] = x as u8;
            planes.push(ofs);
        }
    }

    fn update_from_sei(&mut self, sei: &SeiMessage, plane_id: usize) -> bool {
        let ofmd_match = memmem::Finder::new("OFMD")
            .find(sei.payload)
            .unwrap_or_default();

        if ofmd_match == 0 {
            return false;
        }

        let ofmd = &sei.payload[ofmd_match..];

        let frame_rate = ofmd[4] & 15;
        if !(1..=7).contains(&frame_rate) && frame_rate != 5 {
            return false;
        }

        let frame_count = ofmd[11] as usize & 127;
        self.offsets.extend(
            ofmd.iter()
                .take(14 + (plane_id * frame_count) + frame_count)
                .skip(14 + (plane_id * frame_count)),
        );
        self.number_of_frames += frame_count as u32;

        true
    }

    pub fn get_stats(self, ofs_planes: &[OffsetMetadataSequence]) -> String {
        if self.offsets.is_empty() {
            return format!("3D-Plane #{:02} is empty.\n", self.get_id());
        }

        let new_depths = self
            .offsets
            .iter()
            .map(|&x| if x > 128 { 128 - x as i32 } else { x as i32 })
            .collect::<Vec<i32>>();
        let minval = new_depths.iter().min().unwrap();
        let maxval = new_depths.iter().max().unwrap();

        let total: i32 = new_depths
            .iter()
            .filter_map(|&x| (x != 128).then_some(x))
            .sum();
        let undefined = self
            .offsets
            .iter()
            .filter_map(|&x| (x as usize == 128).then_some(x))
            .count();
        let firstframe = self
            .offsets
            .iter()
            .position(|&x| x != 128)
            .unwrap_or_default();
        let lastframe = self
            .offsets
            .iter()
            .enumerate()
            .rfind(|x| *x.1 != 128)
            .unwrap()
            .0;
        let average = total as f32 / (self.offsets.len() as f32 - undefined as f32);
        let cuts = self.offsets.iter().dedup().count();

        let mut output: String = String::new();

        use std::fmt::Write;
        writeln!(output, "NumFrames: {}", new_depths.len()).unwrap();
        writeln!(output, "Minimum depth: {}", minval).unwrap();
        writeln!(output, "Maximum depth: {}", maxval).unwrap();
        writeln!(output, "Average depth: {:.2}", average).unwrap();
        writeln!(output, "Number of changes of depth value: {}", cuts).unwrap();
        writeln!(output, "First frame with defined depth: {}", firstframe).unwrap();
        writeln!(output, "Last frame with defined depth: {}", lastframe).unwrap();

        let mut identical = String::new();
        for plane in ofs_planes.iter() {
            if plane.offsets == self.offsets && self.guid != plane.guid {
                write!(identical, " {}", plane.get_id()).unwrap();
            }
        }
        if identical.is_empty() {
            identical = String::from(" None");
        }
        writeln!(output, "Identical Planes:{}", identical).unwrap();

        if minval == maxval {
            writeln!(
                output,
                "*** Warning This 3D-Plane has a fixed depth of {}! ***",
                minval
            )
            .unwrap();
        }

        output
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        wincode::serialize(self).unwrap()
    }

    pub fn get_id(&self) -> usize {
        self.guid[15] as usize
    }

    pub fn get_frame_rate(&self) -> usize {
        (self.frame_rate_and_drop_frame >> 4) as usize
    }
}

pub fn create_ofs_file(
    ofs: &OffsetMetadataSequence,
    out_directory: &str,
) -> std::result::Result<(), std::io::Error> {
    let path = Path::new(&out_directory);
    if !path.try_exists()? {
        std::fs::create_dir(path)?;
    }

    if ofs.offsets.is_empty() {
        return Ok(());
    }

    let out_path = path.join(format!("3D-Plane-{:02}.ofs", ofs.get_id()));
    let mut out_file = std::fs::File::create(out_path)?;

    out_file.write_all(&ofs.to_bytes())?;

    Ok(())
}
