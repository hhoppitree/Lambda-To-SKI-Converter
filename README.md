# Lambda-To-SKI-Converter

A tool for converting a lambda expression to SKI combinators.

Use \ to input λ symbol. Here're some examples:

```plain
1. 
Enter Lambda Expression:  \x. \y. y x 
--- Compilation Result ---
SKI Form:    ((S (K (S I))) K)
Lambda Form: λp.λq.(q p)

2. 
Enter Lambda Expression: \f \g g (f x) 
--- Compilation Result ---
SKI Form:    ((S (K (S I))) ((S (K K)) ((S I) (K x))))
Lambda Form: λp.λq.(q (p x))

3.
Enter Lambda Expression: \f. (\x. f (x x)) (\x. f (x x))
--- Compilation Result ---
SKI Form:    ((S ((S ((S (K S)) K)) (K ((S I) I)))) ((S ((S (K S)) K)) (K ((S I) I))))
Segmentation fault
```

