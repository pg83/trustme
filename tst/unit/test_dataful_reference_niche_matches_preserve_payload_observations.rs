enum First<'a> { Data(&'a u32, [u8; 16]), Count(u16) }
enum Last<'a> { Count(u16), Data(&'a u32, [u8; 16]) }
#[inline(never)]
fn first(value: &First<'_>) -> u32 {
    match value { First::Data(number, bytes) => **number + bytes[0] as u32, First::Count(count) => *count as u32 }
}
#[inline(never)]
fn last(value: &Last<'_>) -> u32 {
    match value { Last::Count(count) => *count as u32, Last::Data(number, bytes) => **number + bytes[0] as u32 }
}
fn main() {
    let number = 37;
    assert_eq!(first(&First::Data(&number, [3; 16])), 40);
    assert_eq!(first(&First::Count(41)), 41);
    assert_eq!(last(&Last::Data(&number, [7; 16])), 44);
    assert_eq!(last(&Last::Count(43)), 43);
}
