// axum's `tap_inner!` matches `{ $($stmt:stmt)* }` against statements that
// end in `;`. A `stmt` fragment stops before the semicolon, and the `;` left
// over is itself a statement - upstream `parse_stmt_without_recovery` eats a
// lone `;` as `StmtKind::Empty` - so `a; b;` is four statements. The matcher
// knew no statement starting with `;`: "No arm matched".
macro_rules! tap {
    ($value:ident => { $($stmt:stmt)* }) => {{
        let mut $value = Vec::new();
        $($stmt)*;
        $value
    }};
}

macro_rules! spell {
    ($($stmt:stmt)*) => { [$(stringify!($stmt)),*] };
}

fn main() {
    let v = tap!(items => {
        items.push(1);
        items.push(2);
    });
    assert_eq!(v, vec![1, 2]);
    let w: Vec<u8> = tap!(items => { ; });
    assert!(w.is_empty());
    assert_eq!(spell!(let a = 1; a + 1), ["let a = 1", ";", "a + 1"]);
}
