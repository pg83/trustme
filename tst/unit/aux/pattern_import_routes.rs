pub mod source {
    pub const CODE: u16 = 467;
    pub fn make_code() -> u16 { CODE }
    pub type Count = u16;
}
pub use source::CODE as EXPORTED_CODE;
pub use source::make_code as exported_function;
pub use source::Count as ExportedCount;
