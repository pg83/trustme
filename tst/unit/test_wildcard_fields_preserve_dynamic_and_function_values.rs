struct Record<'a> { function: fn(u16) -> u16, value: &'a dyn std::fmt::Debug }
fn plus(value: u16) -> u16 { value + 7 }
#[inline(never)]
fn inspect(record: &Record<'_>) -> u16 {
    match record { Record { function, value: _ } => function(37) }
}
fn main() {
    let record = Record { function: plus, value: &41u16 };
    assert_eq!(inspect(&record), 44);
    assert_eq!(format!("{:?}", record.value), "41");
}
