use crate::OFMDdata;
use rand::RngExt;
use std::io::Write;
use std::path::Path;

pub fn create_ofs_files(
    ofmd_data: &OFMDdata,
    out_directory: &str,
    drop_frame: bool,
) -> std::result::Result<(), std::io::Error> {
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
    if drop_frame {
        assert!(ofmd_data.frame_rate == 4);
    }
    let signature: [u8; 8] = [0x89, 0x4f, 0x46, 0x53, 0x0d, 0x0a, 0x1a, 0x0a];
    let version: [u8; 4] = [0x30, 0x31, 0x30, 0x30];
    let mut guid: [u8; 16] = std::array::from_fn(|_| rand::rng().random::<u8>());
    let rolls_and_reserved: [u8; 4] = [0x01, 0x00, 0x00, 0x00];
    let timecode: [u8; 4] = [0x00, 0x00, 0x00, 0x00];
    let frame_array: [u8; 4] = (ofmd_data.total_frames as u32).to_le_bytes();

    let path = Path::new(&out_directory);
    if !path.try_exists()? {
        std::fs::create_dir(path)?;
    }

    let frame_rate = (ofmd_data.frame_rate * 16) + drop_frame as u8;

    for plane in 0..ofmd_data.num_of_planes {
        if ofmd_data.planes[plane].is_empty() {
            continue;
        }

        let out_path = path.join(format!("3D-Plane-{:02}.ofs", plane));
        let mut out_file = std::fs::File::create(out_path)?;

        guid[15] = plane as u8;

        let mut buffer = Vec::new();

        buffer.extend_from_slice(&signature);
        buffer.extend_from_slice(&version);
        buffer.extend_from_slice(&guid);
        buffer.extend_from_slice(&[frame_rate]);
        buffer.extend_from_slice(&rolls_and_reserved);
        buffer.extend_from_slice(&timecode);
        buffer.extend_from_slice(&frame_array);
        buffer.extend_from_slice(&ofmd_data.planes[plane]);

        out_file.write_all(&buffer)?;
    }

    Ok(())
}
