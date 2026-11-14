//@ crate-type: lib
//@ compile-flags: -Ddead_code --emit=metadata -Cpanic=abort

#[allow(dead_code)]
fn allowed_function() {}

#[allow(dead_code)]
mod allowed_module {
    fn inherited_allow() {}

    #[warn(dead_code)]
    fn warned_inside_allowed_module() {}
}
