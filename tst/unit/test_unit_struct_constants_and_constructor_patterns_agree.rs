#[derive(PartialEq, Eq)]
struct Stop;
const END: Stop = Stop;

fn constant(value: Stop) -> u8 { match value { END => 17 } }
fn constructor(value: Stop) -> u8 { match value { Stop => 23 } }
fn named(value: Stop) -> u8 { match value { Stop {} => 41 } }

fn main() {
    assert_eq!((constant(Stop), constructor(Stop), named(Stop)), (17, 23, 41));
}
