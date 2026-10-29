macro_rules! adjacent {
    ($(alpha beta),* $(alpha),* done) => { 17usize };
    ($($other:tt)*) => { 91usize };
}

macro_rules! separator_path {
    ($(alpha, beta)* $(alpha),* done) => { 23usize };
    ($($other:tt)*) => { 92usize };
}

macro_rules! separator_mismatch {
    ($(alpha beta omega)* $($(alpha);+ beta),* done) => { 31usize };
    ($($other:tt)*) => { 93usize };
}

fn main() {
    assert_eq!(adjacent!(done), 17);
    assert_eq!(adjacent!(alpha done), 17);
    assert_eq!(adjacent!(alpha beta done), 17);
    assert_eq!(adjacent!(alpha beta, alpha beta alpha, alpha done), 17);
    assert_eq!(adjacent!(beta done), 91);

    assert_eq!(separator_path!(done), 23);
    assert_eq!(separator_path!(alpha, beta done), 23);
    assert_eq!(separator_path!(alpha done), 23);
    assert_eq!(separator_path!(alpha, beta alpha, beta alpha, alpha done), 23);
    assert_eq!(separator_path!(beta done), 92);

    assert_eq!(separator_mismatch!(done), 31);
    assert_eq!(separator_mismatch!(alpha beta omega done), 31);
    assert_eq!(separator_mismatch!(alpha beta done), 31);
    assert_eq!(separator_mismatch!(alpha; alpha beta, alpha beta done), 31);
    assert_eq!(separator_mismatch!(alpha beta omega alpha; alpha beta done), 31);
    assert_eq!(separator_mismatch!(beta done), 93);
}
