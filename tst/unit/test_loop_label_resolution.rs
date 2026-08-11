fn main() {
    let block_value = 'block: {
        break 'block 4;
    };
    assert_eq!(block_value, 4);

    let mut nested = 0;
    'same: loop {
        'same: loop {
            nested = 1;
            break 'same;
        }
        assert_eq!(nested, 1);
        nested = 2;
        break 'same;
    }
    assert_eq!(nested, 2);

    let mut iterations = 0;
    'while_loop: while iterations < 3 {
        iterations += 1;
        continue 'while_loop;
    }
    assert_eq!(iterations, 3);
}
