#![feature(intrinsics)]
#![allow(internal_features)]

macro_rules! declare {
    ($($name:ident),+ $(,)?) => { $(
        #[rustc_intrinsic]
        unsafe fn $name<T: Copy>(destination: *mut T, expected: T, replacement: T) -> (T, bool);
    )+ };
}
declare!(atomic_cxchg_acq_failrelaxed, atomic_cxchg_acqrel_failrelaxed,
    atomic_cxchg_rel, atomic_cxchg_acqrel, atomic_cxchg_failrelaxed,
    atomic_cxchgweak_acq_failrelaxed, atomic_cxchgweak_acqrel_failrelaxed,
    atomic_cxchgweak_failrelaxed, atomic_cxchgweak, atomic_cxchgweak_acq,
    atomic_cxchgweak_rel, atomic_cxchgweak_acqrel, atomic_cxchgweak_relaxed,
    atomic_cxchgweak_acquire);

macro_rules! strong {
    ($name:ident) => {{
        let mut value = 7u32;
        unsafe {
            assert_eq!($name(&mut value, 7, 19), (7, true));
            assert_eq!($name(&mut value, 7, 23), (19, false));
        }
        assert_eq!(value, 19);
    }};
}
macro_rules! weak {
    ($name:ident) => {{
        let mut value = 11u32;
        loop {
            let result = unsafe { $name(&mut value, 11, 29) };
            assert_eq!(result.0, 11);
            if result.1 { break; }
        }
        assert_eq!(unsafe { $name(&mut value, 11, 31) }, (29, false));
        assert_eq!(value, 29);
    }};
}

fn main() {
    strong!(atomic_cxchg_acq_failrelaxed);
    strong!(atomic_cxchg_acqrel_failrelaxed);
    strong!(atomic_cxchg_rel);
    strong!(atomic_cxchg_acqrel);
    strong!(atomic_cxchg_failrelaxed);
    weak!(atomic_cxchgweak_acq_failrelaxed);
    weak!(atomic_cxchgweak_acqrel_failrelaxed);
    weak!(atomic_cxchgweak_failrelaxed);
    weak!(atomic_cxchgweak);
    weak!(atomic_cxchgweak_acq);
    weak!(atomic_cxchgweak_rel);
    weak!(atomic_cxchgweak_acqrel);
    weak!(atomic_cxchgweak_relaxed);
    weak!(atomic_cxchgweak_acquire);
}
