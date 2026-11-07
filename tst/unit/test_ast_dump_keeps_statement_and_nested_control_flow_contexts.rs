//@ compile-flags: -Zdump-ast
//@ env: TRUSTME_CONTROL_FLOW_AST=$OUT_1_ast.rs

fn exercise(statement_tag: u8) -> (u8, u8, u8, u8, u8) {
    let mut ticks = 0u8;
    'statement_loop: loop {
        ticks += 1;
        break 'statement_loop;
    }
    let from_loop = loop { break 9u8; };
    'statement_while: while ticks < 3 {
        ticks += 1;
    }
    let from_while = (while false {}, ticks).1;
    match statement_tag {
        0 => ticks += 7,
        _ => ticks += 11,
    }
    let from_match = match ticks {
        10 => 17u8,
        14 => 19u8,
        _ => 0,
    };
    if statement_tag == 0 {
        ticks += 1;
    } else if statement_tag == 1 {
        ticks += 2;
    } else {
        ticks += 3;
    }
    let from_if = if statement_tag == 0 { 23u8 } else { 29u8 };
    (ticks, from_loop, from_while, from_match, from_if)
}

fn main() {
    assert_eq!(exercise(0), (11, 9, 3, 17, 23));
    assert_eq!(exercise(1), (16, 9, 3, 19, 29));
    assert_eq!(exercise(2), (17, 9, 3, 19, 29));

    let path = std::env::var("TRUSTME_CONTROL_FLOW_AST").unwrap();
    let dump = std::fs::read_to_string(path).unwrap();
    let lines: Vec<_> = dump.lines().collect();
    let root_loop = lines.iter().position(|line| {
        line.trim_start().starts_with("'statement_loop") && line.contains(": loop")
    }).unwrap();
    assert!(lines[root_loop].trim_end().ends_with("loop"));
    assert_eq!(lines[root_loop + 1].trim(), "{");
    let root_while = lines.iter().position(|line| {
        line.trim_start().starts_with("'statement_while") && line.contains(": while ")
    }).unwrap();
    assert_eq!(lines[root_while + 1].trim(), "{");
    let root_match = lines.iter().position(|line| {
        line.trim_start().starts_with("match ") && line.contains("statement_tag")
    }).unwrap();
    assert_eq!(lines[root_match + 1].trim(), "{");
    assert!(lines.iter().any(|line| line.trim_start().starts_with("else if ")));
    assert!(lines.iter().any(|line| line.trim_start().starts_with("else{")));
}
