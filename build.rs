fn main() {
    let out_dir = std::env::var_os("OUT_DIR").unwrap();
    let path = std::path::Path::new(&out_dir).join("license.rs");

    let new_func = "pub fn get_license() -> &'static str { \n\"".to_string();
    let close_func = "\"\n}".to_string();
    let mut license_string = std::fs::read_to_string("LICENSE").expect("Error reading 'LICENSE'");
    license_string = license_string.replace("\"", "\\\"");

    let license = new_func + &license_string + &close_func;

    std::fs::write(&path, license).unwrap();
    println!("cargo:rerun-if-changed=build.rs");
}
