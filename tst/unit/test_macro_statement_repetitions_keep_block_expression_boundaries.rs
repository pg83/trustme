macro_rules! sequence { ($($statement:stmt)*) => {{ $($statement)* }}; }
fn main() {
    let mut value = 0;
    sequence!({ value += 2; } (value += 3););
    sequence!({ value += 5; } [()][0];);
    sequence!(if value == 10 { value += 7; } value += 11;);
    assert_eq!(value, 28);
}
