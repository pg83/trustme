//@ edition: 2021
//@ compile-flags: -Z dump-ast
//@ compile-fail: placement syntax is not supported

fn main() {
    0 <- 1;
}
