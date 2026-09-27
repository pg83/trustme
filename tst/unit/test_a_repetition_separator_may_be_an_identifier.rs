// proc-macro2's tests/marker.rs matches `$($marker:ident) and +` and
// `$($marker:ident) or +`. rustc's `parse_sep_and_kleene_op` takes any token
// other than a Kleene operator as the separator - an identifier included -
// and the transcriber may repeat with one (`$($x) and *`). We rejected a
// separator that carries data ("Invalid macro joiner ..., must be
// punctuation") and kept only the token kind of the one we took.
macro_rules! count {
    ($($m:ident) and +) => {
        [$(stringify!($m)),+].len()
    };
    ($($m:ident) or +) => {
        0 $(+ { let _ = stringify!($m); 10 })+
    };
}

macro_rules! join {
    ($($m:ident),*) => {
        stringify!($($m) and *)
    };
}

fn main() {
    assert_eq!(count!(Send and Sync and Copy), 3);
    assert_eq!(count!(Send or Sync), 20);
    assert_eq!(count!(Send), 1);
    assert_eq!(join!(a, b, c), "a and b and c");
}
