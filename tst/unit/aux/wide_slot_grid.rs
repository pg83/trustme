macro_rules! define_wide {
    (@collect $kind:ident [$($decl:tt)*] [$($arg:tt)*] [$($name:tt)*] [$($identity:tt)*];) => {
        define_wide!(@finish $kind [$($decl)*] [$($arg)*] [$($name)*] [$($identity)*]);
    };
    (@collect $kind:ident [$($decl:tt)*] [$($arg:tt)*] [$($name:tt)*] [$($identity:tt)*];
        ($head:ident, $ty:ty, $base:literal) $($tail:tt)*) => {
        define_wide!(@collect $kind [
            $($decl)*
            ${concat($head, _A_T)}, const ${concat($head, _A_N)}: usize,
            ${concat($head, _B_T)}, const ${concat($head, _B_N)}: usize,
            ${concat($head, _C_T)}, const ${concat($head, _C_N)}: usize,
            ${concat($head, _D_T)}, const ${concat($head, _D_N)}: usize,
            ${concat($head, _E_T)}, const ${concat($head, _E_N)}: usize,
            ${concat($head, _F_T)}, const ${concat($head, _F_N)}: usize,
            ${concat($head, _G_T)}, const ${concat($head, _G_N)}: usize,
            ${concat($head, _H_T)}, const ${concat($head, _H_N)}: usize,
            ${concat($head, _I_T)}, const ${concat($head, _I_N)}: usize,
            ${concat($head, _J_T)}, const ${concat($head, _J_N)}: usize,
            ${concat($head, _K_T)}, const ${concat($head, _K_N)}: usize,
            ${concat($head, _L_T)}, const ${concat($head, _L_N)}: usize,
            ${concat($head, _M_T)}, const ${concat($head, _M_N)}: usize,
            ${concat($head, _N_T)}, const ${concat($head, _N_N)}: usize,
            ${concat($head, _O_T)}, const ${concat($head, _O_N)}: usize,
            ${concat($head, _P_T)}, const ${concat($head, _P_N)}: usize,
        ] [
            $($arg)*
            $ty, { $base + 0 },
            $ty, { $base + 1 },
            $ty, { $base + 2 },
            $ty, { $base + 3 },
            $ty, { $base + 4 },
            $ty, { $base + 5 },
            $ty, { $base + 6 },
            $ty, { $base + 7 },
            $ty, { $base + 8 },
            $ty, { $base + 9 },
            $ty, { $base + 10 },
            $ty, { $base + 11 },
            $ty, { $base + 12 },
            $ty, { $base + 13 },
            $ty, { $base + 14 },
            $ty, { $base + 15 },
        ] [
            $($name)*
            ${concat($head, _A_T)},
            ${concat($head, _B_T)},
            ${concat($head, _C_T)},
            ${concat($head, _D_T)},
            ${concat($head, _E_T)},
            ${concat($head, _F_T)},
            ${concat($head, _G_T)},
            ${concat($head, _H_T)},
            ${concat($head, _I_T)},
            ${concat($head, _J_T)},
            ${concat($head, _K_T)},
            ${concat($head, _L_T)},
            ${concat($head, _M_T)},
            ${concat($head, _N_T)},
            ${concat($head, _O_T)},
            ${concat($head, _P_T)},
        ] [
            $($identity)*
            ${concat($head, _A_T)}, ${concat($head, _A_N)},
            ${concat($head, _B_T)}, ${concat($head, _B_N)},
            ${concat($head, _C_T)}, ${concat($head, _C_N)},
            ${concat($head, _D_T)}, ${concat($head, _D_N)},
            ${concat($head, _E_T)}, ${concat($head, _E_N)},
            ${concat($head, _F_T)}, ${concat($head, _F_N)},
            ${concat($head, _G_T)}, ${concat($head, _G_N)},
            ${concat($head, _H_T)}, ${concat($head, _H_N)},
            ${concat($head, _I_T)}, ${concat($head, _I_N)},
            ${concat($head, _J_T)}, ${concat($head, _J_N)},
            ${concat($head, _K_T)}, ${concat($head, _K_N)},
            ${concat($head, _L_T)}, ${concat($head, _L_N)},
            ${concat($head, _M_T)}, ${concat($head, _M_N)},
            ${concat($head, _N_T)}, ${concat($head, _N_N)},
            ${concat($head, _O_T)}, ${concat($head, _O_N)},
            ${concat($head, _P_T)}, ${concat($head, _P_N)},
        ]; $($tail)*);
    };
    (@finish function [$($decl:tt)*] [$($arg:tt)*] [$($name:tt)*] [$($identity:tt)*]) => {
        fn echo<$($decl)* LAST_T, const LAST_N: usize>(
            values: (P_P_T, Q_A_T, AF_P_T, LAST_T),
        ) -> ((P_P_T, Q_A_T, AF_P_T, LAST_T), [usize; 4]) {
            let widths = || [P_P_N, Q_A_N, AF_P_N, LAST_N];
            (values, widths())
        }
        fn main() {
            let result = echo::<$($arg)* u128, 512>((11u16, 13u32, 17u64, 19u128));
            assert_eq!(result, ((11, 13, 17, 19), [255, 256, 511, 512]));
        }
    };
    (@finish packet [$($decl:tt)*] [$($arg:tt)*] [$($name:tt)*] [$($identity:tt)*]) => {
        pub struct Packet<$($decl)* LAST_T, const LAST_N: usize> {
            pub data: (P_P_T, Q_A_T, AF_P_T, LAST_T),
            pub bytes: [u8; LAST_N],
            pub marker: core::marker::PhantomData<($($name)* LAST_T)>,
        }
        impl<$($decl)* LAST_T, const LAST_N: usize> Packet<$($identity)* LAST_T, LAST_N> {
            pub fn indices(&self) -> [usize; 4] {
                [P_P_N, Q_A_N, AF_P_N, LAST_N]
            }
            pub fn echo(self) -> Self {
                self
            }
        }
        pub fn make() -> Packet<$($arg)* u128, 5> {
            Packet {
                data: (251u16, 257u32, 509u64, 1021u128),
                bytes: [29; 5],
                marker: core::marker::PhantomData,
            }
        }
    };
}
