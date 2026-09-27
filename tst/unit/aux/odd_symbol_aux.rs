#[unsafe(export_name = "trustme\n\"odd\" name\t`x`\\")]
pub extern "C" fn defined() -> i32 {
    7
}
