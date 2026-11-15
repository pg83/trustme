//@ aux-build: cov99_w1345_legacy_const.rs

enum Token {
    Only,
}

fn main() {
    let value = cov99_w1345_legacy_const::value(match Token::Only {
        Token::Only => 37,
    });
    assert_eq!(value, 37);
}
