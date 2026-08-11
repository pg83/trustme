trait Marker<'a> {}

fn select<'a>(opaque: impl Marker<'a>, input: &u32) -> &u32 {
    let _ = opaque;
    input
}

fn main() {}
