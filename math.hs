
binop2string :: (Eq a, Fractional a) => (a -> a -> a) -> [Char]
binop2string f = test $ f 1 1
  where
    test 2 = "+"
    test 1 = "*"
    test 0 = "-"

h :: (Eq a, Fractional a) => a -> [a -> a -> a] -> [(a, [a -> a -> a])]
h 0 f = [(0, [])]
h n f = concatMap applyEach $ h (n - 1) f
  where applyEach (x,p) = [(f0 x n, f0:p) | f0 <- f]

printh :: (Show a, Fractional a, Eq a) => [(a, [a -> a -> a])] -> [Char]
printh = concatMap printSingle
  where
    printSingle :: (Show a, Fractional a, Eq a) => (a, [a -> a -> a]) -> [Char]
    printSingle (res, x:xs) = binop2string x ++ printSingle (res, xs)
    printSingle (res, []) = " " ++ show res ++ "\n"
