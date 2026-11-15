//@ compile-fail: type annotations needed

fn main() {
    let pointer;
    let _: () = return *pointer;
}
