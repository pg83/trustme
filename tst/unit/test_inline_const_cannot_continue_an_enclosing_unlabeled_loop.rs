//@ compile-fail: error:

fn main() {
    loop {
        let _: () = const {
            continue;
        };
    }
}
