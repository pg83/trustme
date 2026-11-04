pub fn value() -> u32 { 197 }

#[macro_export]
macro_rules! expose {
    ($relay:ident) => { $relay!(use $crate::value as imported;); };
}
