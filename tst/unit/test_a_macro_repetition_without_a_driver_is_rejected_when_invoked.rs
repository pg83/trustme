//@ reject-flags: --cfg invoke => error || error
macro_rules! no_driver {
    () => { $( const _: u8 = 5; )* };
}

fn main() {
    #[cfg(invoke)]
    no_driver!();
    assert_eq!(std::mem::size_of::<[u16; 3]>(), 6);
}
