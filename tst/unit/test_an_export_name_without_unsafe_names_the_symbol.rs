// `#[export_name = "..."]` without `unsafe(...)` is accepted before edition
// 2024 and names the symbol just as `#[unsafe(export_name = "...")]` does
// (rustc_codegen_ssa's codegen_fn_attrs); only the `unsafe(...)` spelling was
// read, so the function kept its mangled name and the link failed.
#[export_name = "trustme_bare_export_name"]
pub extern "C" fn defined() -> i32 {
    7
}

extern "C" {
    #[link_name = "trustme_bare_export_name"]
    fn declared() -> i32;
}

fn main() {
    assert_eq!(unsafe { declared() }, 7);
    assert_eq!(defined(), 7);
}
