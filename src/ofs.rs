use crate::OFMDdata;
use rand::RngExt;
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
#[derive(SchemaWrite, SchemaRead)]
struct OffsetMetadataSequence {
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
    pub fn new(ofmd_data: &OFMDdata, plane: usize, drop_frame: bool) -> OffsetMetadataSequence {
        OffsetMetadataSequence {
            signature: [0x89, 0x4f, 0x46, 0x53, 0x0d, 0x0a, 0x1a, 0x0a],
            version: [0x30, 0x31, 0x30, 0x30],
            frame_rate_and_drop_frame: (ofmd_data.frame_rate * 16) + drop_frame as u8,
            guid: Self::get_guid(true),
            rolls_and_reserved: [0; 4],
            timecode: [0; 4],
            number_of_frames: ofmd_data.total_frames as u32,
            offsets: ofmd_data.planes[plane].clone(),
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        wincode::serialize(self).unwrap()
    }

    fn get_guid(random: bool) -> [u8; 16] {
        if !random {
            return [0; 16];
        }
        std::array::from_fn(|_| rand::rng().random::<u8>())
    }
}

pub fn create_ofs_files(
    ofmd_data: &OFMDdata,
    out_directory: &str,
    drop_frame: bool,
) -> std::result::Result<(), std::io::Error> {
    if drop_frame {
        assert!(ofmd_data.frame_rate == 4);
    }

    let path = Path::new(&out_directory);
    if !path.try_exists()? {
        std::fs::create_dir(path)?;
    }

    for plane in 0..ofmd_data.num_of_planes {
        if ofmd_data.planes[plane].is_empty() {
            continue;
        }

        let out_path = path.join(format!("3D-Plane-{:02}.ofs", plane));
        let mut out_file = std::fs::File::create(out_path)?;

        let mut ofs = OffsetMetadataSequence::new(ofmd_data, plane, drop_frame);
        ofs.guid[15] = plane as u8;

        out_file.write_all(&ofs.to_bytes())?;
    }

    Ok(())
}
