fn increment(value: u16) -> u16 { value + 7 }

fn main() {
    let byte = 37u8;
    let pair = std::hint::black_box((increment as fn(u16) -> u16, &byte as *const u8));
    let result = match pair {
        (function, pointer) => (function(19), unsafe { *pointer }),
    };
    assert_eq!(result, (26, 37));
    let pair = std::hint::black_box((increment, 13u16));
    let result = match pair { (function, value) => function(value) };
    assert_eq!(result, 20);
}
