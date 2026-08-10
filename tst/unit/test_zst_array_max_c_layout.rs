struct Huge([(); usize::MAX]);

#[inline(never)]
fn length(value: Huge) -> usize {
    value.0.len()
}

fn main() {
    assert_eq!(length(Huge([(); usize::MAX])), usize::MAX);
}
