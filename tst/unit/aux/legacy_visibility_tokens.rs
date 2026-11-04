//@ edition: 2015

#[macro_export]
macro_rules! relative {
    ($name:ident) => { pub(in outer) fn $name() -> u16 { 113 } };
}

#[macro_export]
macro_rules! absolute {
    ($name:ident) => { pub(in ::outer) fn $name() -> u16 { 127 } };
}
