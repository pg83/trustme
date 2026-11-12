//@ compile-fail: error:

fn main() {
    'outer: loop {
        let _: () = const {
            continue 'outer;
        };
    }
}
