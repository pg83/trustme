//@ compile-fail: include! requires a string literal

fn main() {
    include!(false);
}
