from .common import Benchmark, safe_import

with safe_import():
    from scipy.sparse import random

    # enable threading
    try:
        import scipy.sparse._sparsetools
        scipy.sparse._sparsetools.set_workers(0)
    except AttributeError:
        pass


class BenchMatrixPower(Benchmark):
    params = [
        [0, 1, 2, 3, 8, 9],
        [1000, 2000],
        [1e-6, 1e-3, 0.01],
    ]
    param_names = ['x', 'N', 'density']

    def setup(self, x: int, N: int, density: float):
        self.A = random(N, N, density=density, format='csr')

    def time_matrix_power(self, x: int, N: int, density: float):
        self.A ** x
