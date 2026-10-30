struct Slots<F> { left:F, right:F }
struct Pair<F>(F,F);
struct Ops;
impl Ops {
 fn first(value:u8)->u16 { value as u16+10 }
 fn second(value:u8)->u16 { value as u16*2 }
}
fn main() {
 let tuple:(fn(u8)->u16,fn(u8)->u16)=(Ops::first,Ops::second);
 assert_eq!((tuple.0(3),tuple.1(4)),(13,8));
 let slots=Slots::<fn(u8)->u16>{left:Ops::first,right:Ops::second};
 assert_eq!((slots.left)(3)+(slots.right)(4),21);
 let pair=Pair::<fn(u8)->u16>(Ops::first,Ops::second);
 assert_eq!(pair.0(5)+pair.1(6),27);
 let array=[Ops::first,Ops::second];
 assert_eq!(array[0](7)+array[1](8),33);
}
