#![feature(macro_metavar_expr_concat)]
#![recursion_limit = "256"]

macro_rules! build {
    (@collect [$($decl:tt)*] [$($arg:tt)*];) => {
        type Tail<$($decl)* const LAST: usize> = [u8; LAST];

        fn main() {
            let values: Tail<$($arg)* 5> = [29; 5];
            assert_eq!(values, [29; 5]);
        }
    };
    (@collect [$($decl:tt)*] [$($arg:tt)*]; $head:ident $($tail:ident)*) => {
        build!(@collect [
            $($decl)*
            const ${concat($head, _A)}: usize,
            const ${concat($head, _B)}: usize,
            const ${concat($head, _C)}: usize,
            const ${concat($head, _D)}: usize,
            const ${concat($head, _E)}: usize,
            const ${concat($head, _F)}: usize,
            const ${concat($head, _G)}: usize,
            const ${concat($head, _H)}: usize,
            const ${concat($head, _I)}: usize,
            const ${concat($head, _J)}: usize,
            const ${concat($head, _K)}: usize,
            const ${concat($head, _L)}: usize,
            const ${concat($head, _M)}: usize,
            const ${concat($head, _N)}: usize,
            const ${concat($head, _O)}: usize,
            const ${concat($head, _P)}: usize,
        ] [
            $($arg)* 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]; $($tail)*);
    };
}

build!(@collect [] [];
    A B C D E F G H I J K L M N O P
    Q R S T U V W X Y Z AA AB AC AD AE AF
    AG AH AI AJ AK AL AM AN AO AP AQ AR AS AT AU AV
);
