#[macro_export]
macro_rules! choose_tokens {
    (invalid) => { $( const _: u8 = 7; )* };
    (value) => { 31u8 };
}
