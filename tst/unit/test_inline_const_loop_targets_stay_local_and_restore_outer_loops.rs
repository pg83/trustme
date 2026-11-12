fn main() {
    let mut total = 0u16;
    'outer: loop {
        let value = const {
            'value: {
                let mut count = 0u16;
                'inside: loop {
                    count += 1;
                    if count == 1 {
                        continue;
                    }
                    if count == 2 {
                        continue 'inside;
                    }
                    break 'value count;
                }
            }
        };
        assert_eq!(value, 3);
        total += value;
        if total < 6 {
            continue 'outer;
        }
        break 'outer;
    }
    assert_eq!(total, 6);
}
