fn number(value: u16) -> u16 {
    match value {
        1 => 19,
        ..3 => 23,
        5..=7 => 29,
        9..12 => 31,
        15.. => 37,
        _ => 41,
    }
}

fn letter(value: char) -> u16 {
    match value {
        'α'..='γ' => 43,
        'λ' => 47,
        _ => 53,
    }
}

fn main() {
    for (value, expected) in [(1, 19), (0, 23), (2, 23), (5, 29), (7, 29), (9, 31), (11, 31), (15, 37), (99, 37), (12, 41)] {
        assert_eq!(number(value), expected);
    }
    assert_eq!(letter('β'), 43);
    assert_eq!(letter('λ'), 47);
    assert_eq!(letter('z'), 53);
    assert_eq!(match true { true => 59, _ => 61 }, 59);
    assert_eq!(match "leaf" { "root" => 67, "leaf" => 71, _ => 73 }, 71);
}
