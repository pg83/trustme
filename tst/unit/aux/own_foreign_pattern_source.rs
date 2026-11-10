pub struct Mark;

#[macro_export]
macro_rules! relay_pattern {
    ($sink:ident) => { $sink!($crate::Mark) };
}
