// tower-layer implements `Layer<S>` for tuples of up to sixteen layers,
// `impl<S, L1, L2, ..> Layer<S> for (L1, L2, ..) where L1: Layer<L2::Service>,
// L2: Layer<L3::Service>, ..` with `type Service = L1::Service`. Each level
// of that projection nests the next, and we normalized the associated type
// through the solver, which asked for every nested projection again at every
// level - tower_layer took an hour and 31 GB, and every project on tower
// timed out. In rustc a projection whose self is a type parameter is rigid
// unless the environment has a projection predicate for it, so the declared
// type stays as written, and `ProjectionCache` answers a rigid projection
// once.
pub trait Layer<S> {
    type Service;
    fn layer(&self, inner: S) -> Self::Service;
}
impl<S, L1> Layer<S> for (L1,)
where
    L1: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1,) = self;
        l1.layer(service)
    }
}
impl<S, L1, L2> Layer<S> for (L1, L2)
where
    L1: Layer<L2::Service>,
    L2: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2) = self;
        l1.layer(l2.layer(service))
    }
}
impl<S, L1, L2, L3> Layer<S> for (L1, L2, L3)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3) = self;
        l1.layer(l2.layer(l3.layer(service)))
    }
}
impl<S, L1, L2, L3, L4> Layer<S> for (L1, L2, L3, L4)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(service))))
    }
}
impl<S, L1, L2, L3, L4, L5> Layer<S> for (L1, L2, L3, L4, L5)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(service)))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6> Layer<S> for (L1, L2, L3, L4, L5, L6)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(service))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7> Layer<S> for (L1, L2, L3, L4, L5, L6, L7)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(service)))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(service))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(service)))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(service))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(service)))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<L12::Service>,
    L12: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(l12.layer(service))))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<L12::Service>,
    L12: Layer<L13::Service>,
    L13: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(l12.layer(l13.layer(service)))))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<L12::Service>,
    L12: Layer<L13::Service>,
    L13: Layer<L14::Service>,
    L14: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(l12.layer(l13.layer(l14.layer(service))))))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14, L15> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14, L15)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<L12::Service>,
    L12: Layer<L13::Service>,
    L13: Layer<L14::Service>,
    L14: Layer<L15::Service>,
    L15: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(l12.layer(l13.layer(l14.layer(l15.layer(service)))))))))))))))
    }
}
impl<S, L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14, L15, L16> Layer<S> for (L1, L2, L3, L4, L5, L6, L7, L8, L9, L10, L11, L12, L13, L14, L15, L16)
where
    L1: Layer<L2::Service>,
    L2: Layer<L3::Service>,
    L3: Layer<L4::Service>,
    L4: Layer<L5::Service>,
    L5: Layer<L6::Service>,
    L6: Layer<L7::Service>,
    L7: Layer<L8::Service>,
    L8: Layer<L9::Service>,
    L9: Layer<L10::Service>,
    L10: Layer<L11::Service>,
    L11: Layer<L12::Service>,
    L12: Layer<L13::Service>,
    L13: Layer<L14::Service>,
    L14: Layer<L15::Service>,
    L15: Layer<L16::Service>,
    L16: Layer<S>,
{
    type Service = L1::Service;
    fn layer(&self, service: S) -> Self::Service {
        let (l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15, l16) = self;
        l1.layer(l2.layer(l3.layer(l4.layer(l5.layer(l6.layer(l7.layer(l8.layer(l9.layer(l10.layer(l11.layer(l12.layer(l13.layer(l14.layer(l15.layer(l16.layer(service))))))))))))))))
    }
}
struct Add(u32);
impl Layer<u32> for Add {
    type Service = u32;
    fn layer(&self, s: u32) -> u32 {
        s + self.0
    }
}
fn main() {
    assert_eq!((Add(1), Add(2)).layer(3), 6);
    assert_eq!((Add(1), Add(2), Add(3), Add(4), Add(5), Add(6), Add(7), Add(8), Add(9), Add(10), Add(11), Add(12), Add(13), Add(14), Add(15), Add(16)).layer(0), 136);
}
