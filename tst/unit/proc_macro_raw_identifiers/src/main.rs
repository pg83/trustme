use raw_identifiers_macro::raw_idents;

pub enum Expr {
    Position { expr: u8, r#in: u8 },
}

#[raw_idents]
fn position(e: &Expr) -> u8 {
    match e {
        Expr::Position { expr, r#in } => expr + r#in,
    }
}

fn main() {
    assert_eq!(position(&Expr::Position { expr: 1, r#in: 2 }), 3);
    assert_eq!(BARE, 0);
    assert!(RAW >= 2);
}
