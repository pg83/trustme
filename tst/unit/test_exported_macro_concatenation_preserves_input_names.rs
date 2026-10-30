//@ aux-build: concat_routes.rs
#![feature(macro_metavar_expr_concat)]
concat_routes::routes!(alpha, 17);
mod nested { concat_routes::routes!(beta, 31); }
fn main() {
    assert_eq!((alpha_value(), read_alpha()), (17, 18));
    assert_eq!((nested::beta_value(), nested::read_beta()), (31, 32));
}
