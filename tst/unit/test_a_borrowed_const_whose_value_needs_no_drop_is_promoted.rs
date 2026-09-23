// `&CONST` is promoted to a static when the constant's value needs no drop,
// judged by the value (upstream's ConstQualifs: an aggregate needs drop only
// through a Drop impl of its own or an operand that does), not by its type.
// `Tok::Eof` needs no drop although `Tok` has a `String` variant. It was
// judged by the type, so `&EOF` was a temporary in the caller's frame and a
// `-> &'static` function returned a dangling pointer: sqlparser's
// `peek_nth_token_ref` returns `&EOF_TOKEN` and its tests read garbage.
#[allow(dead_code)]
enum Tok {
    Word(String),
    Eof,
}

#[allow(dead_code)]
struct Spanned {
    token: Tok,
    at: (u64, u64),
}

const EOF: Tok = Tok::Eof;
const EOF_SPANNED: Spanned = Spanned { token: EOF, at: (0, 0) };

fn eof() -> &'static Tok {
    &EOF
}

fn eof_spanned() -> &'static Spanned {
    &EOF_SPANNED
}

fn first_or_eof(tokens: &[Spanned]) -> &Spanned {
    tokens.first().unwrap_or(&EOF_SPANNED)
}

#[inline(never)]
fn deep(n: u32) -> (*const Tok, *const Spanned) {
    if n == 0 {
        (eof(), first_or_eof(&[]))
    } else {
        let pad = [n; 64];
        let inner = deep(n - 1);
        std::hint::black_box(&pad);
        inner
    }
}

fn main() {
    let (tok, spanned) = deep(8);
    assert!(std::ptr::eq(tok, eof()));
    assert!(std::ptr::eq(spanned, eof_spanned()));
    let spanned = unsafe { &*spanned };
    assert!(matches!(spanned.token, Tok::Eof));
    assert_eq!(spanned.at, (0, 0));
}
