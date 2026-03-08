use simpleargs::arg::ArgString;
use simpleargs::{Arg, Args, OptionError, UsageError};
use std::ffi::OsString;
use std::io::Write;
use std::path::Path;

mod ofs;
use ofs::OffsetMetadataSequence;

include!(concat!(env!("OUT_DIR"), "/license.rs"));

fn print_title() {
    println!(
        "{} {} {} by {}",
        env!("CARGO_PKG_NAME"),
        env!("CARGO_PKG_VERSION"),
        std::env::consts::ARCH,
        env!("CARGO_PKG_AUTHORS")
    );
}

fn print_usage() {
    let args = std::env::args().next().unwrap();
    let path = std::path::Path::new(&args);

    println!(
        "Usage: {} [-help] [-license] <input file> <output folder> [-fps # -dropframe]",
        path.file_name().unwrap().to_string_lossy()
    );
}

fn print_help() {
    print_usage();
    println!(
        "
  -help : Prints what you're currently reading.

  -license : Print license (MIT).

  <input file> : Can be raw MVC stream, a H264+MVC combined stream (like those from MakeMKV).
                 Using '-' will read from stdin.

  <output folder> : The output folder which will contain the ofs files.
                    If undefined the current directory will be used.

Advanced Options: Use with care!

  -fps # : Must be a value between 1 and 4, 6, or 7. See table.
           This will override the fps value that was sourced by the input file.

           FPS Conversion Table:
           1 : 23.976
           2 : 24
           3 : 25
           4 : 29.97
           6 : 50
           7 : 59.94

  -dropframe : Set drop_frame_flag within the resulting OFS files.
               Can only be use with FPS value 4.
        "
    );
}

fn print_license() {
    println!("{}", get_license());
}

struct OFSExtractArguments {
    help: bool,
    input: OsString,
    output_directory: OsString,
    license: bool,
    frame_rate_option: Option<i32>,
    drop_frame: bool,
}

fn parse_args<T>(mut args: Args<T>) -> Result<OFSExtractArguments, UsageError<OsString>>
where
    T: Iterator<Item = OsString>,
{
    let mut result = OFSExtractArguments {
        help: false,
        input: "".into(),
        output_directory: "".into(),
        license: false,
        frame_rate_option: None,
        drop_frame: false,
    };
    let mut input: Option<OsString> = None;
    let mut output_directory: Option<OsString> = None;

    loop {
        match args.next() {
            Arg::Positional(arg) => {
                if input.is_some() && output_directory.is_some() {
                    return Err(UsageError::UnexpectedArgument { arg });
                }

                if input.is_none() {
                    input = Some(arg)
                } else if output_directory.is_none() {
                    output_directory = Some(arg)
                }
            }
            Arg::Named(arg) => arg.parse(|name, value| match name {
                "help" => {
                    result.help = true;
                    Ok(())
                }
                "fps" => {
                    let fps = value.as_str()?.parse()?;
                    if !((1..=7).contains(&fps) && fps != 5) {
                        return Err(OptionError::InvalidValue(
                            Err::<T, &str>("<1, 2, 3, 4, 6, or 7>")
                                .err()
                                .unwrap()
                                .into(),
                        ));
                    }
                    result.frame_rate_option = Some(fps);
                    Ok(())
                }
                "dropframe" => {
                    result.drop_frame = true;
                    Ok(())
                }
                "license" => {
                    result.license = true;
                    Ok(())
                }
                _ => Err(OptionError::Unknown),
            })?,
            Arg::End => break,
            Arg::Error(err) => return Err(err),
        }
    }
    if result.help || result.license {
        return Ok(result);
    }
    if result.drop_frame
        && result.frame_rate_option.is_some()
        && result.frame_rate_option.unwrap() != 4
    {
        return Err(UsageError::InvalidArgument {
            arg: "-fps must be 4 to use -dropframe"
                .to_string()
                .to_osstr()
                .into(),
        });
    }

    result.input = match input {
        Some(path) => path,
        None => {
            return Err(UsageError::MissingArgument {
                name: "input file".to_owned(),
            });
        }
    };
    result.output_directory = match output_directory {
        Some(path) => path,
        None => std::env::current_dir()
            .expect("There's a problem with your current working directory!")
            .into(),
    };

    Ok(result)
}

fn main() -> Result<(), std::io::Error> {
    print_title();

    let mut os_args = std::env::args_os();
    os_args.next();

    if os_args.len() == 0 {
        print_help();
        std::process::exit(exitcode::USAGE);
    }

    let arguments = match parse_args(Args::from(os_args)) {
        Ok(arguments) => arguments,
        Err(e) => {
            print_usage();
            println!("\n{}", e);
            std::process::exit(exitcode::USAGE);
        }
    };

    if arguments.help {
        print_help();
        return Ok(());
    }

    if arguments.license {
        print_license();
        return Ok(());
    }

    let ofs_planes = match OffsetMetadataSequence::get_from_h264(
        &arguments.input.to_string_lossy(),
        arguments.drop_frame,
    ) {
        Err(e) => {
            println!("Error: {}", e);
            std::process::exit(exitcode::IOERR);
        }
        Ok(x) => x,
    };

    let frame_rate = ofs_planes[0].get_frame_rate();

    if frame_rate != 4 && arguments.drop_frame {
        println!(
            "Source fps, '{}', is not compatible with '-dropframe'!",
            frame_rate
        );
        std::process::exit(exitcode::USAGE);
    }

    for plane in ofs_planes.iter() {
        println!("{}", plane.clone().get_stats(&ofs_planes));

        let path = Path::new(&arguments.output_directory);
        if !path.try_exists()? {
            std::fs::create_dir(path)?;
        }

        if plane.is_empty() {
            continue;
        }

        let out_path = path.join(format!("3D-Plane-{:02}.ofs", plane.get_id()));
        let mut out_file = std::fs::File::create(out_path)?;

        out_file.write_all(&plane.to_bytes())?;
    }

    std::process::exit(exitcode::OK);
}
