mod outer {
    pub fn value() -> u16 {
        mod local { pub static VALUE: u16 = 43; }
        use local::VALUE as SAVED;
        *(&SAVED)
    }
}
fn main() { assert_eq!(outer::value(), 43); }
