#![feature(macro_metavar_expr_concat)]
macro_rules! getters {
    ($($name:ident => $value:expr),+) => {
        $(fn ${concat(read_, $name)}() -> u16 { $value })+
    };
}
getters!(alpha => 11, beta => 17, gamma => 23);
fn main() { assert_eq!((read_alpha(), read_beta(), read_gamma()), (11, 17, 23)); }
