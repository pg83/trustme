struct Token;

impl Drop for Token {
    fn drop(&mut self) {
        panic!("unreachable token drop");
    }
}

enum MaybePair {
    Pair(Token, Token),
    Empty,
}

fn main() {
    let pair;
    if false {
        pair = MaybePair::Pair(Token, Token);
        if let MaybePair::Pair(first, _) = pair {
            drop(first);
        }
    }
}
