// A matched fragment is upstream's `TokenStream::from_ast`: the tokens it was
// written with, in an invisible group. They are what a `macro_rules!`
// written by another macro holds where the fragment was substituted
// (petgraph's tests use `defmac!`, which puts `$p:pat` fragments into the
// macro it defines), and what `stringify!` prints for it.
macro_rules! defmac {
    (@nest $name:ident ($dol:tt) => ([$($arg:ident)*] $($result_body:tt)+)) => {
        macro_rules! $name {
            ($($dol $arg : expr), *) => {
                $($result_body)+
            }
        }
    };
    (@nest $name:ident ($dol:tt) => ([$($arg:ident)*] $($result_body:tt)+) $p1:pat $(, $p2:pat)*) => {
        defmac!{
            @nest $name ($dol) => ([marg $($arg)*] match {$dol marg} { $p1 => $($result_body)+ })
            $($p2),*
        }
    };
    ($name:ident $($p:pat),* => $result:expr) => {
        defmac!(@nest $name ($) => ([] $result) $($p),*);
    };
}
macro_rules! gen { ($name:ident, $e:expr) => { macro_rules! $name { () => { $e * 2 } } } }
macro_rules! show { ($e:expr) => { stringify!($e) } }
defmac!(first ref v, x => v[x] + 1);
defmac!(both (a, b) => a + b);
gen!(dbl, 1 + 2);
fn main() {
    let v = vec![1, 2, 3];
    assert_eq!(first!(1, v), 3);
    assert_eq!(both!((4, 5)), 9);
    assert_eq!(dbl!(), 6);
    assert_eq!(show!(a+b), "a+b");
    assert_eq!(show!(f( x )), "f(x)");
}
