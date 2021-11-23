from functools import partial
import math
import threading
import pickle
import pytest
from copy import deepcopy
import numpy as np
from numpy.testing import assert_allclose, assert_equal, suppress_warnings
from numpy.lib import NumpyVersion
from scipy.stats import (
    TransformedDensityRejection,
    DiscreteAliasUrn,
    DiscreteGuideTable,
    NumericalInversePolynomial,
    NumericalInverseHermite,
    NaiveRatioUniforms
)
from scipy.stats import UNURANError
from scipy import stats
from scipy import special
from scipy.stats import chisquare, cramervonmises
from scipy.stats._distr_params import distdiscrete, distcont
from scipy._lib._util import check_random_state


# common test data: this data can be shared between all the tests.


# Normal distribution shared between all the continuous methods
class StandardNormal:
    def pdf(self, x):
        # normalization constant needed for NumericalInverseHermite
        return 1./np.sqrt(2.*np.pi) * np.exp(-0.5 * x*x)

    def dpdf(self, x):
        return 1./np.sqrt(2.*np.pi) * -x * np.exp(-0.5 * x*x)

    def cdf(self, x):
        return special.ndtr(x)


all_methods = [
    ("TransformedDensityRejection", {"dist": StandardNormal()}),
    ("DiscreteAliasUrn", {"dist": [0.02, 0.18, 0.8]}),
    ("DiscreteGuideTable", {"dist": [0.02, 0.18, 0.8]}),
    ("NumericalInversePolynomial", {"dist": StandardNormal()}),
    ("NumericalInverseHermite", {"dist": StandardNormal()}),
    ("NaiveRatioUniforms", {"dist": StandardNormal(),
                            "u_min": -np.exp(-0.5) * np.sqrt(2),
                            "u_max": np.exp(-0.5) * np.sqrt(2), "v_max": 1.0})
]

# Make sure an internal error occurs in UNU.RAN when invalid callbacks are
# passed. Moreover, different generators throw different error messages.
# So, in case of an `UNURANError`, we do not validate the error message.
bad_pdfs_common = [
    # Negative PDF
    (lambda x: -x, UNURANError, r"..."),
    # Returning wrong type
    (lambda x: [], TypeError, r"must be real number, not list"),
    # Undefined name inside the function
    (lambda x: foo, NameError, r"name 'foo' is not defined"),  # type: ignore[name-defined]  # noqa
    # Infinite value returned => Overflow error.
    (lambda x: np.inf, UNURANError, r"..."),
    # NaN value => internal error in UNU.RAN
    (lambda x: np.nan, UNURANError, r"..."),
    # signature of PDF wrong
    (lambda: 1.0, TypeError, r"takes 0 positional arguments but 1 was given")
]

# Make sure an internal error occurs in UNU.RAN when invalid callbacks are
# passed. Moreover, different generators throw different error messages.
# So, in case of an `UNURANError`, we do not validate the messages.
bad_dpdf_common = [
    # Infinite value returned.
    (lambda x: np.inf, UNURANError, r"..."),
    # NaN value => internal error in UNU.RAN
    (lambda x: np.nan, UNURANError, r"..."),
    # Returning wrong type
    (lambda x: [], TypeError, r"must be real number, not list"),
    # Undefined name inside the function
    (lambda x: foo, NameError, r"name 'foo' is not defined"),  # type: ignore[name-defined]  # noqa
    # signature of dPDF wrong
    (lambda: 1.0, TypeError, r"takes 0 positional arguments but 1 was given")
]


bad_pv_common = [
    ([], r"must contain at least one element"),
    ([[1.0, 0.0]], r"wrong number of dimensions \(expected 1, got 2\)"),
    ([0.2, 0.4, np.nan, 0.8], r"must contain only finite / non-nan values"),
    ([0.2, 0.4, np.inf, 0.8], r"must contain only finite / non-nan values"),
    ([0.0, 0.0], r"must contain at least one non-zero value"),
]


# size of the domains is incorrect
bad_sized_domains = [
    # > 2 elements in the domain
    ((1, 2, 3), ValueError, r"must be a length 2 tuple"),
    # empty domain
    ((), ValueError, r"must be a length 2 tuple")
]

# domain values are incorrect
bad_domains = [
    ((2, 1), UNURANError, r"left >= right"),
    ((1, 1), UNURANError, r"left >= right"),
]

# infinite and nan values present in domain.
inf_nan_domains = [
    # left >= right
    ((10, 10), UNURANError, r"left >= right"),
    ((np.inf, np.inf), UNURANError, r"left >= right"),
    ((-np.inf, -np.inf), UNURANError, r"left >= right"),
    ((np.inf, -np.inf), UNURANError, r"left >= right"),
    # Also include nans in some of the domains.
    ((-np.inf, np.nan), ValueError, r"only non-nan values"),
    ((np.nan, np.inf), ValueError, r"only non-nan values")
]

# `nan` values present in domain. Some distributions don't support
# infinite tails, so don't mix the nan values with infinities.
nan_domains = [
    ((0, np.nan), ValueError, r"only non-nan values"),
    ((np.nan, np.nan), ValueError, r"only non-nan values")
]


# all the methods should throw errors for nan, bad sized, and bad valued
# domains.
@pytest.mark.parametrize("domain, err, msg",
                         bad_domains + bad_sized_domains +
                         nan_domains)  # type: ignore[operator]
@pytest.mark.parametrize("method, kwargs", all_methods)
def test_bad_domain(domain, err, msg, method, kwargs):
    Method = getattr(stats, method)
    with pytest.raises(err, match=msg):
        Method(**kwargs, domain=domain)


@pytest.mark.parametrize("method, kwargs", all_methods)
def test_random_state(method, kwargs):
    Method = getattr(stats, method)

    # simple seed that works for any version of NumPy
    seed = 123
    rng1 = Method(**kwargs, random_state=seed)
    rng2 = Method(**kwargs, random_state=seed)
    assert_equal(rng1.rvs(100), rng2.rvs(100))

    # global seed
    np.random.seed(123)
    rng1 = Method(**kwargs)
    rvs1 = rng1.rvs(100)
    np.random.seed(None)
    rng2 = Method(**kwargs, random_state=123)
    rvs2 = rng2.rvs(100)
    assert_equal(rvs1, rvs2)

    # RandomState seed for old numpy
    if NumpyVersion(np.__version__) < '1.19.0':
        seed1 = np.random.RandomState(123)
        seed2 = 123
        rng1 = Method(**kwargs, random_state=seed1)
        rng2 = Method(**kwargs, random_state=seed2)
        assert_equal(rng1.rvs(100), rng2.rvs(100))
        rvs11 = rng1.rvs(550)
        rvs12 = rng1.rvs(50)
        rvs2 = rng2.rvs(600)
        assert_equal(rvs11, rvs2[:550])
        assert_equal(rvs12, rvs2[550:])
    else:  # Generator seed for new NumPy
        # when a RandomState is given, it should take the bitgen_t
        # member of the class and create a Generator instance.
        seed1 = np.random.RandomState(np.random.MT19937(123))
        seed2 = np.random.Generator(np.random.MT19937(123))
        rng1 = Method(**kwargs, random_state=seed1)
        rng2 = Method(**kwargs, random_state=seed2)
        assert_equal(rng1.rvs(100), rng2.rvs(100))


def test_set_random_state():
    rng1 = TransformedDensityRejection(StandardNormal(), random_state=123)
    rng2 = TransformedDensityRejection(StandardNormal())
    rng2.set_random_state(123)
    assert_equal(rng1.rvs(100), rng2.rvs(100))
    rng = TransformedDensityRejection(StandardNormal(), random_state=123)
    rvs1 = rng.rvs(100)
    rng.set_random_state(123)
    rvs2 = rng.rvs(100)
    assert_equal(rvs1, rvs2)


def test_threading_behaviour():
    # Test if the API is thread-safe.
    # This verifies if the lock mechanism and the use of `PyErr_Occurred`
    # is correct.
    errors = {"err1": None, "err2": None}

    class Distribution:
        def __init__(self, pdf_msg):
            self.pdf_msg = pdf_msg

        def pdf(self, x):
            if 49.9 < x < 50.0:
                raise ValueError(self.pdf_msg)
            return x

        def dpdf(self, x):
            return 1

    def func1():
        dist = Distribution('foo')
        rng = TransformedDensityRejection(dist, domain=(10, 100),
                                          random_state=12)
        try:
            rng.rvs(100000)
        except ValueError as e:
            errors['err1'] = e.args[0]

    def func2():
        dist = Distribution('bar')
        rng = TransformedDensityRejection(dist, domain=(10, 100),
                                          random_state=2)
        try:
            rng.rvs(100000)
        except ValueError as e:
            errors['err2'] = e.args[0]

    t1 = threading.Thread(target=func1)
    t2 = threading.Thread(target=func2)

    t1.start()
    t2.start()

    t1.join()
    t2.join()

    assert errors['err1'] == 'foo'
    assert errors['err2'] == 'bar'


@pytest.mark.parametrize("method, kwargs", all_methods)
def test_pickle(method, kwargs):
    Method = getattr(stats, method)
    rng1 = Method(**kwargs, random_state=123)
    obj = pickle.dumps(rng1)
    rng2 = pickle.loads(obj)
    assert_equal(rng1.rvs(100), rng2.rvs(100))


@pytest.mark.parametrize("size", [None, 0, (0, ), 1, (10, 3), (2, 3, 4, 5),
                                  (0, 0), (0, 1)])
def test_rvs_size(size):
    # As the `rvs` method is present in the base class and shared between
    # all the classes, we can just test with one of the methods.
    rng = TransformedDensityRejection(StandardNormal())
    if size is None:
        assert np.isscalar(rng.rvs(size))
    else:
        if np.isscalar(size):
            size = (size, )
        assert rng.rvs(size).shape == size


def test_with_scipy_distribution():
    # test if the setup works with SciPy's rv_frozen distributions
    dist = stats.norm()
    urng = np.random.default_rng(0)
    rng = NumericalInverseHermite(dist, random_state=urng)
    u = np.linspace(0, 1, num=100)
    check_cont_samples(rng, dist, dist.stats())
    assert_allclose(dist.ppf(u), rng.ppf(u))
    # test if it works with `loc` and `scale`
    dist = stats.norm(loc=10., scale=5.)
    rng = NumericalInverseHermite(dist, random_state=urng)
    check_cont_samples(rng, dist, dist.stats())
    assert_allclose(dist.ppf(u), rng.ppf(u))
    # check for discrete distributions
    dist = stats.binom(10, 0.2)
    rng = DiscreteAliasUrn(dist, random_state=urng)
    domain = dist.support()
    pv = dist.pmf(np.arange(domain[0], domain[1]+1))
    check_discr_samples(rng, pv, dist.stats())


def check_cont_samples(rng, dist, mv_ex):
    rvs = rng.rvs(100000)
    mv = rvs.mean(), rvs.var()
    # test the moments only if the variance is finite
    if np.isfinite(mv_ex[1]):
        assert_allclose(mv, mv_ex, rtol=1e-7, atol=1e-1)
    # Cramer Von Mises test for goodness-of-fit
    rvs = rng.rvs(500)
    dist.cdf = np.vectorize(dist.cdf)
    pval = cramervonmises(rvs, dist.cdf).pvalue
    assert pval > 0.1


def check_discr_samples(rng, pv, mv_ex):
    rvs = rng.rvs(100000)
    # test if the first few moments match
    mv = rvs.mean(), rvs.var()
    assert_allclose(mv, mv_ex, rtol=1e-3, atol=1e-1)
    # normalize
    pv = pv / pv.sum()
    # chi-squared test for goodness-of-fit
    obs_freqs = np.zeros_like(pv)
    _, freqs = np.unique(rvs, return_counts=True)
    freqs = freqs / freqs.sum()
    obs_freqs[:freqs.size] = freqs
    pval = chisquare(obs_freqs, pv).pvalue
    assert pval > 0.1


class TestTransformedDensityRejection:
    # Simple Custom Distribution
    class dist0:
        def pdf(self, x):
            return 3/4 * (1-x*x)

        def dpdf(self, x):
            return 3/4 * (-2*x)

        def cdf(self, x):
            return 3/4 * (x - x**3/3 + 2/3)

        def support(self):
            return -1, 1

    # Standard Normal Distribution
    class dist1:
        def pdf(self, x):
            return stats.norm._pdf(x / 0.1)

        def dpdf(self, x):
            return -x / 0.01 * stats.norm._pdf(x / 0.1)

        def cdf(self, x):
            return stats.norm._cdf(x / 0.1)

    # pdf with piecewise linear function as transformed density
    # with T = -1/sqrt with shift. Taken from UNU.RAN test suite
    # (from file t_tdr_ps.c)
    class dist2:
        def __init__(self, shift):
            self.shift = shift

        def pdf(self, x):
            x -= self.shift
            y = 1. / (abs(x) + 1.)
            return 0.5 * y * y

        def dpdf(self, x):
            x -= self.shift
            y = 1. / (abs(x) + 1.)
            y = y * y * y
            return y if (x < 0.) else -y

        def cdf(self, x):
            x -= self.shift
            if x <= 0.:
                return 0.5 / (1. - x)
            else:
                return 1. - 0.5 / (1. + x)

    dists = [dist0(), dist1(), dist2(0.), dist2(10000.)]

    # exact mean and variance of the distributions in the list dists
    mv0 = [0., 4./15.]
    mv1 = [0., 0.01]
    mv2 = [0., np.inf]
    mv3 = [10000., np.inf]
    mvs = [mv0, mv1, mv2, mv3]

    @pytest.mark.parametrize("dist, mv_ex",
                             zip(dists, mvs))
    def test_basic(self, dist, mv_ex):
        with suppress_warnings() as sup:
            # filter the warnings thrown by UNU.RAN
            sup.filter(RuntimeWarning)
            rng = TransformedDensityRejection(dist, random_state=42)
        check_cont_samples(rng, dist, mv_ex)

    # PDF 0 everywhere => bad construction points
    bad_pdfs = [(lambda x: 0, UNURANError, r"50 : bad construction points.")]
    bad_pdfs += bad_pdfs_common  # type: ignore[arg-type]

    @pytest.mark.parametrize("pdf, err, msg", bad_pdfs)
    def test_bad_pdf(self, pdf, err, msg):
        class dist:
            pass
        dist.pdf = pdf
        dist.dpdf = lambda x: 1  # an arbitrary dPDF
        with pytest.raises(err, match=msg):
            TransformedDensityRejection(dist)

    @pytest.mark.parametrize("dpdf, err, msg", bad_dpdf_common)
    def test_bad_dpdf(self, dpdf, err, msg):
        class dist:
            pass
        dist.pdf = lambda x: x
        dist.dpdf = dpdf
        with pytest.raises(err, match=msg):
            TransformedDensityRejection(dist, domain=(1, 10))

    # test domains with inf + nan in them. need to write a custom test for
    # this because not all methods support infinite tails.
    @pytest.mark.parametrize("domain, err, msg", inf_nan_domains)
    def test_inf_nan_domains(self, domain, err, msg):
        with pytest.raises(err, match=msg):
            TransformedDensityRejection(StandardNormal(), domain=domain)

    @pytest.mark.parametrize("construction_points", [-1, 0, 0.1])
    def test_bad_construction_points_scalar(self, construction_points):
        with pytest.raises(ValueError, match=r"`construction_points` must be "
                                             r"a positive integer."):
            TransformedDensityRejection(
                StandardNormal(), construction_points=construction_points
            )

    def test_bad_construction_points_array(self):
        # empty array
        construction_points = []
        with pytest.raises(ValueError, match=r"`construction_points` must "
                                             r"either be a "
                                             r"scalar or a non-empty array."):
            TransformedDensityRejection(
                StandardNormal(), construction_points=construction_points
            )

        # construction_points not monotonically increasing
        construction_points = [1, 1, 1, 1, 1, 1]
        with pytest.warns(RuntimeWarning, match=r"33 : starting points not "
                                                r"strictly monotonically "
                                                r"increasing"):
            TransformedDensityRejection(
                StandardNormal(), construction_points=construction_points
            )

        # construction_points containing nans
        construction_points = [np.nan, np.nan, np.nan]
        with pytest.raises(UNURANError, match=r"50 : bad construction "
                                              r"points."):
            TransformedDensityRejection(
                StandardNormal(), construction_points=construction_points
            )

        # construction_points out of domain
        construction_points = [-10, 10]
        with pytest.warns(RuntimeWarning, match=r"50 : starting point out of "
                                                r"domain"):
            TransformedDensityRejection(
                StandardNormal(), domain=(-3, 3),
                construction_points=construction_points
            )

    @pytest.mark.parametrize("c", [-1., np.nan, np.inf, 0.1, 1.])
    def test_bad_c(self, c):
        msg = r"`c` must either be -0.5 or 0."
        with pytest.raises(ValueError, match=msg):
            TransformedDensityRejection(StandardNormal(), c=-1.)

    u = [np.linspace(0, 1, num=1000), [], [[]], [np.nan],
         [-np.inf, np.nan, np.inf], 0,
         [[np.nan, 0.5, 0.1], [0.2, 0.4, np.inf], [-2, 3, 4]]]

    @pytest.mark.parametrize("u", u)
    def test_ppf_hat(self, u):
        # Increase the `max_squeeze_hat_ratio` so the ppf_hat is more
        # accurate.
        rng = TransformedDensityRejection(StandardNormal(),
                                          max_squeeze_hat_ratio=0.9999)
        # Older versions of NumPy throw RuntimeWarnings for comparisons
        # with nan.
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "invalid value encountered in greater")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "greater_equal")
            sup.filter(RuntimeWarning, "invalid value encountered in less")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "less_equal")
            res = rng.ppf_hat(u)
            expected = stats.norm.ppf(u)
        assert_allclose(res, expected, rtol=1e-3, atol=1e-5)
        assert res.shape == expected.shape

    def test_bad_dist(self):
        # Empty distribution
        class dist:
            ...

        msg = r"`pdf` required but not found."
        with pytest.raises(ValueError, match=msg):
            TransformedDensityRejection(dist)

        # dPDF not present in dist
        class dist:
            pdf = lambda x: 1-x*x  # noqa: E731

        msg = r"`dpdf` required but not found."
        with pytest.raises(ValueError, match=msg):
            TransformedDensityRejection(dist)


class TestDiscreteAliasUrn:
    # DAU fails on these probably because of large domains and small
    # computation errors in PMF. Mean/SD match but chi-squared test fails.
    basic_fail_dists = {
        'nchypergeom_fisher',  # numerical erros on tails
        'nchypergeom_wallenius',  # numerical erros on tails
        'randint'  # fails on 32-bit ubuntu
    }

    @pytest.mark.parametrize("distname, params", distdiscrete)
    def test_basic(self, distname, params):
        if distname in self.basic_fail_dists:
            msg = ("DAU fails on these probably because of large domains "
                   "and small computation errors in PMF.")
            pytest.skip(msg)
        if not isinstance(distname, str):
            dist = distname
        else:
            dist = getattr(stats, distname)
        dist = dist(*params)
        domain = dist.support()
        if not np.isfinite(domain[1] - domain[0]):
            # DAU only works with finite domain. So, skip the distributions
            # with infinite tails.
            pytest.skip("DAU only works with a finite domain.")
        k = np.arange(domain[0], domain[1]+1)
        pv = dist.pmf(k)
        mv_ex = dist.stats('mv')
        rng = DiscreteAliasUrn(dist, random_state=42)
        check_discr_samples(rng, pv, mv_ex)

    # Can't use bad_pmf_common here as we evaluate PMF early on to avoid
    # unhelpful errors from UNU.RAN.
    bad_pmf = [
        # inf returned
        (lambda x: np.inf, ValueError,
         r"must contain only finite / non-nan values"),
        # nan returned
        (lambda x: np.nan, ValueError,
         r"must contain only finite / non-nan values"),
        # all zeros
        (lambda x: 0.0, ValueError,
         r"must contain at least one non-zero value"),
        # Undefined name inside the function
        (lambda x: foo, NameError,  # type: ignore[name-defined]  # noqa
         r"name 'foo' is not defined"),
        # Returning wrong type.
        (lambda x: [], ValueError,
         r"setting an array element with a sequence."),
        # probabilities < 0
        (lambda x: -x, UNURANError,
         r"50 : probability < 0"),
        # signature of PMF wrong
        (lambda: 1.0, TypeError,
         r"takes 0 positional arguments but 1 was given")
    ]

    @pytest.mark.parametrize("pmf, err, msg", bad_pmf)
    def test_bad_pmf(self, pmf, err, msg):
        class dist:
            pass
        dist.pmf = pmf
        with pytest.raises(err, match=msg):
            DiscreteAliasUrn(dist, domain=(1, 10))

    @pytest.mark.parametrize("pv", [[0.18, 0.02, 0.8],
                                    [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]])
    def test_sampling_with_pv(self, pv):
        pv = np.asarray(pv, dtype=np.float64)
        rng = DiscreteAliasUrn(pv, random_state=123)
        rvs = rng.rvs(100_000)
        pv = pv / pv.sum()
        variates = np.arange(0, len(pv))
        # test if the first few moments match
        m_expected = np.average(variates, weights=pv)
        v_expected = np.average((variates - m_expected) ** 2, weights=pv)
        mv_expected = m_expected, v_expected
        check_discr_samples(rng, pv, mv_expected)

    @pytest.mark.parametrize("pv, msg", bad_pv_common)
    def test_bad_pv(self, pv, msg):
        with pytest.raises(ValueError, match=msg):
            DiscreteAliasUrn(pv)

    # DAU doesn't support infinite tails. So, it should throw an error when
    # inf is present in the domain.
    inf_domain = [(-np.inf, np.inf), (np.inf, np.inf), (-np.inf, -np.inf),
                  (0, np.inf), (-np.inf, 0)]

    @pytest.mark.parametrize("domain", inf_domain)
    def test_inf_domain(self, domain):
        with pytest.raises(ValueError, match=r"must be finite"):
            DiscreteAliasUrn(stats.binom(10, 0.2), domain=domain)

    def test_bad_urn_factor(self):
        with pytest.warns(RuntimeWarning, match=r"relative urn size < 1."):
            DiscreteAliasUrn([0.5, 0.5], urn_factor=-1)

    def test_bad_args(self):
        msg = (r"`domain` must be provided when the "
               r"probability vector is not available.")

        class dist:
            def pmf(self, x):
                return x

        with pytest.raises(ValueError, match=msg):
            DiscreteAliasUrn(dist)


class TestNumericalInversePolynomial:
    # Simple Custom Distribution
    class dist0:
        def pdf(self, x):
            return 3/4 * (1-x*x)

        def cdf(self, x):
            return 3/4 * (x - x**3/3 + 2/3)

        def support(self):
            return -1, 1

    # Standard Normal Distribution
    class dist1:
        def pdf(self, x):
            return stats.norm._pdf(x / 0.1)

        def cdf(self, x):
            return stats.norm._cdf(x / 0.1)

    # Sin 2 distribution
    #          /  0.05 + 0.45*(1 +sin(2 Pi x))  if |x| <= 1
    #  f(x) = <
    #          \  0        otherwise
    # Taken from UNU.RAN test suite (from file t_pinv.c)
    class dist2:
        def pdf(self, x):
            return 0.05 + 0.45 * (1 + np.sin(2*np.pi*x))

        def cdf(self, x):
            return (0.05*(x + 1) +
                    0.9*(1. + 2.*np.pi*(1 + x) - np.cos(2.*np.pi*x)) /
                    (4.*np.pi))

        def support(self):
            return -1, 1

    # Sin 10 distribution
    #          /  0.05 + 0.45*(1 +sin(2 Pi x))  if |x| <= 5
    #  f(x) = <
    #          \  0        otherwise
    # Taken from UNU.RAN test suite (from file t_pinv.c)
    class dist3:
        def pdf(self, x):
            return 0.2 * (0.05 + 0.45 * (1 + np.sin(2*np.pi*x)))

        def cdf(self, x):
            return x/10. + 0.5 + 0.09/(2*np.pi) * (np.cos(10*np.pi) -
                                                   np.cos(2*np.pi*x))

        def support(self):
            return -5, 5

    dists = [dist0(), dist1(), dist2(), dist3()]

    # exact mean and variance of the distributions in the list dists
    mv0 = [0., 4./15.]
    mv1 = [0., 0.01]
    mv2 = [-0.45/np.pi, 2/3*0.5 - 0.45**2/np.pi**2]
    mv3 = [-0.45/np.pi, 0.2 * 250/3 * 0.5 - 0.45**2/np.pi**2]
    mvs = [mv0, mv1, mv2, mv3]

    @pytest.mark.parametrize("dist, mv_ex",
                             zip(dists, mvs))
    def test_basic(self, dist, mv_ex):
        rng = NumericalInversePolynomial(dist, random_state=42)
        check_cont_samples(rng, dist, mv_ex)

    very_slow_dists = ['studentized_range', 'trapezoid', 'triang', 'vonmises',
                       'levy_stable', 'kappa4', 'ksone', 'kstwo', 'levy_l',
                       'gausshyper', 'anglit']
    # for some reason, UNU.RAN segmentation faults for the uniform.
    fatal_fail_dists = ['uniform']
    # fails for unbounded PDFs
    unbounded_pdf_fail_dists = ['beta']
    # for these distributions, some assertions fail due to minor
    # numerical differences. They can be avoided either by changing
    # the seed or by increasing the u_resolution.
    fail_dists = ['ncf', 'pareto', 'chi2', 'fatiguelife', 'halfgennorm',
                  'gilbrat', 'lognorm', 'ncx2', 't']

    @pytest.mark.xslow
    @pytest.mark.parametrize("distname, params", distcont)
    def test_basic_all_scipy_dists(self, distname, params):
        if distname in self.very_slow_dists:
            pytest.skip(f"PINV too slow for {distname}")
        if distname in self.fail_dists:
            pytest.skip(f"PINV fails for {distname}")
        if distname in self.unbounded_pdf_fail_dists:
            pytest.skip("PINV fails for unbounded PDFs.")
        if distname in self.fatal_fail_dists:
            pytest.xfail(f"PINV segmentation faults for {distname}")
        dist = (getattr(stats, distname)
                if isinstance(distname, str)
                else distname)
        dist = dist(*params)
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning)
            rng = NumericalInversePolynomial(dist, random_state=42)
        check_cont_samples(rng, dist, [dist.mean(), dist.var()])

    @pytest.mark.parametrize("pdf, err, msg", bad_pdfs_common)
    def test_bad_pdf(self, pdf, err, msg):
        class dist:
            pass
        dist.pdf = pdf
        with pytest.raises(err, match=msg):
            NumericalInversePolynomial(dist, domain=[0, 5])

    # test domains with inf + nan in them. need to write a custom test for
    # this because not all methods support infinite tails.
    @pytest.mark.parametrize("domain, err, msg", inf_nan_domains)
    def test_inf_nan_domains(self, domain, err, msg):
        with pytest.raises(err, match=msg):
            NumericalInversePolynomial(StandardNormal(), domain=domain)

    u = [
        # test if quantile 0 and 1 return -inf and inf respectively and check
        # the correctness of the PPF for equidistant points between 0 and 1.
        np.linspace(0, 1, num=10000),
        # test the PPF method for empty arrays
        [], [[]],
        # test if nans and infs return nan result.
        [np.nan], [-np.inf, np.nan, np.inf],
        # test if a scalar is returned for a scalar input.
        0,
        # test for arrays with nans, values greater than 1 and less than 0,
        # and some valid values.
        [[np.nan, 0.5, 0.1], [0.2, 0.4, np.inf], [-2, 3, 4]]
    ]

    @pytest.mark.parametrize("u", u)
    def test_ppf(self, u):
        dist = StandardNormal()
        rng = NumericalInversePolynomial(dist, u_resolution=1e-14)
        # Older versions of NumPy throw RuntimeWarnings for comparisons
        # with nan.
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "invalid value encountered in greater")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "greater_equal")
            sup.filter(RuntimeWarning, "invalid value encountered in less")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "less_equal")
            res = rng.ppf(u)
            expected = stats.norm.ppf(u)
        assert_allclose(res, expected, rtol=1e-11, atol=1e-11)
        assert res.shape == expected.shape

    x = [np.linspace(-10, 10, num=10000), [], [[]], [np.nan],
         [-np.inf, np.nan, np.inf], 0,
         [[np.nan, 0.5, 0.1], [0.2, 0.4, np.inf], [-np.inf, 3, 4]]]

    @pytest.mark.parametrize("x", x)
    def test_cdf(self, x):
        dist = StandardNormal()
        rng = NumericalInversePolynomial(dist, u_resolution=1e-14)
        # Older versions of NumPy throw RuntimeWarnings for comparisons
        # with nan.
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "invalid value encountered in greater")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "greater_equal")
            sup.filter(RuntimeWarning, "invalid value encountered in less")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "less_equal")
            res = rng.cdf(x)
            expected = stats.norm.cdf(x)
        assert_allclose(res, expected, rtol=1e-11, atol=1e-11)
        assert res.shape == expected.shape

    def test_u_error(self):
        dist = StandardNormal()
        rng = NumericalInversePolynomial(dist, u_resolution=1e-10)
        max_error, mae = rng.u_error()
        assert max_error < 1e-10
        assert mae <= max_error
        rng = NumericalInversePolynomial(dist, u_resolution=1e-14)
        max_error, mae = rng.u_error()
        assert max_error < 1e-14
        assert mae <= max_error

    bad_orders = [1, 4.5, 20, np.inf, np.nan]
    bad_u_resolution = [1e-20, 1e-1, np.inf, np.nan]

    @pytest.mark.parametrize("order", bad_orders)
    def test_bad_orders(self, order):
        dist = StandardNormal()

        msg = r"`order` must be an integer in the range \[3, 17\]."
        with pytest.raises(ValueError, match=msg):
            NumericalInversePolynomial(dist, order=order)

    @pytest.mark.parametrize("u_resolution", bad_u_resolution)
    def test_bad_u_resolution(self, u_resolution):
        msg = r"`u_resolution` must be between 1e-15 and 1e-5."
        with pytest.raises(ValueError, match=msg):
            NumericalInversePolynomial(StandardNormal(),
                                       u_resolution=u_resolution)

    def test_bad_args(self):
        dist = StandardNormal()
        rng = NumericalInversePolynomial(dist)
        msg = r"`sample_size` must be greater than or equal to 1000."
        with pytest.raises(ValueError, match=msg):
            rng.u_error(10)

        class Distribution:
            def pdf(self, x):
                return np.exp(-0.5 * x*x)

        dist = Distribution()
        rng = NumericalInversePolynomial(dist)
        msg = r"Exact CDF required but not found."
        with pytest.raises(ValueError, match=msg):
            rng.u_error()


class TestNumericalInverseHermite:
    #         /  (1 +sin(2 Pi x))/2  if |x| <= 1
    # f(x) = <
    #         \  0        otherwise
    # Taken from UNU.RAN test suite (from file t_hinv.c)
    class dist0:
        def pdf(self, x):
            return 0.5*(1. + np.sin(2.*np.pi*x))

        def dpdf(self, x):
            return np.pi*np.cos(2.*np.pi*x)

        def cdf(self, x):
            return (1. + 2.*np.pi*(1 + x) - np.cos(2.*np.pi*x)) / (4.*np.pi)

        def support(self):
            return -1, 1

    #         /  Max(sin(2 Pi x)),0)Pi/2  if -1 < x <0.5
    # f(x) = <
    #         \  0        otherwise
    # Taken from UNU.RAN test suite (from file t_hinv.c)
    class dist1:
        def pdf(self, x):
            if (x <= -0.5):
                return np.sin((2. * np.pi) * x) * 0.5 * np.pi
            if (x < 0.):
                return 0.
            if (x <= 0.5):
                return np.sin((2. * np.pi) * x) * 0.5 * np.pi

        def dpdf(self, x):
            if (x <= -0.5):
                return np.cos((2. * np.pi) * x) * np.pi * np.pi
            if (x < 0.):
                return 0.
            if (x <= 0.5):
                return np.cos((2. * np.pi) * x) * np.pi * np.pi

        def cdf(self, x):
            if (x <= -0.5):
                return 0.25 * (1 - np.cos((2. * np.pi) * x))
            if (x < 0.):
                return 0.5
            if (x <= 0.5):
                return 0.75 - 0.25 * np.cos((2. * np.pi) * x)

        def support(self):
            return -1, 0.5

    dists = [dist0(), dist1()]

    # exact mean and variance of the distributions in the list dists
    mv0 = [-1/(2*np.pi), 1/3 - 1/(4*np.pi*np.pi)]
    mv1 = [-1/4, 3/8-1/(2*np.pi*np.pi) - 1/16]
    mvs = [mv0, mv1]

    @pytest.mark.parametrize("dist, mv_ex",
                             zip(dists, mvs))
    @pytest.mark.parametrize("order", [3, 5])
    def test_basic(self, dist, mv_ex, order):
        rng = NumericalInverseHermite(dist, order=order, random_state=42)
        check_cont_samples(rng, dist, mv_ex)

    # test domains with inf + nan in them. need to write a custom test for
    # this because not all methods support infinite tails.
    @pytest.mark.parametrize("domain, err, msg", inf_nan_domains)
    def test_inf_nan_domains(self, domain, err, msg):
        with pytest.raises(err, match=msg):
            NumericalInverseHermite(StandardNormal(), domain=domain)

    @pytest.mark.xslow
    @pytest.mark.parametrize(("distname", "shapes"), distcont)
    def test_basic_all_scipy_dists(self, distname, shapes):
        slow_dists = {'ksone', 'kstwo', 'levy_stable', 'skewnorm'}
        fail_dists = {'beta', 'gausshyper', 'geninvgauss', 'ncf', 'nct',
                      'norminvgauss', 'genhyperbolic', 'studentized_range',
                      'vonmises', 'kappa4', 'invgauss', 'wald'}

        if distname in slow_dists:
            pytest.skip("Distribution is too slow")
        if distname in fail_dists:
            # specific reasons documented in gh-13319
            # https://github.com/scipy/scipy/pull/13319#discussion_r626188955
            pytest.xfail("Fails - usually due to inaccurate CDF/PDF")

        np.random.seed(0)

        dist = getattr(stats, distname)(*shapes)

        with np.testing.suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "overflow encountered")
            sup.filter(RuntimeWarning, "divide by zero")
            sup.filter(RuntimeWarning, "invalid value encountered")
            fni = stats.NumericalInverseHermite(dist)

        x = np.random.rand(10)
        p_tol = np.max(np.abs(dist.ppf(x)-fni.ppf(x))/np.abs(dist.ppf(x)))
        u_tol = np.max(np.abs(dist.cdf(fni.ppf(x)) - x))

        assert p_tol < 1e-8
        assert u_tol < 1e-12

    def test_input_validation(self):
        match = r"`order` must be either 1, 3, or 5."
        with pytest.raises(ValueError, match=match):
            NumericalInverseHermite(StandardNormal(), order=2)

        match = "`cdf` required but not found"
        with pytest.raises(ValueError, match=match):
            stats.NumericalInverseHermite("norm")

        match = "could not convert string to float"
        with pytest.raises(ValueError, match=match):
            stats.NumericalInverseHermite(StandardNormal(),
                                          u_resolution='ekki')

        match = "`max_intervals' must be..."
        with pytest.raises(ValueError, match=match):
            stats.NumericalInverseHermite(StandardNormal(), max_intervals=-1)

        match = "`qmc_engine` must be an instance of..."
        with pytest.raises(ValueError, match=match):
            fni = stats.NumericalInverseHermite(StandardNormal())
            fni.qrvs(qmc_engine=0)

        if NumpyVersion(np.__version__) >= '1.18.0':
            # issues with QMCEngines and old NumPy
            fni = stats.NumericalInverseHermite(StandardNormal())

            match = "`d` must be consistent with dimension of `qmc_engine`."
            with pytest.raises(ValueError, match=match):
                fni.qrvs(d=3, qmc_engine=stats.qmc.Halton(2))

    rngs = [None, 0, np.random.RandomState(0)]
    if NumpyVersion(np.__version__) >= '1.18.0':
        rngs.append(np.random.default_rng(0))  # type: ignore
    sizes = [(None, tuple()), (8, (8,)), ((4, 5, 6), (4, 5, 6))]

    @pytest.mark.parametrize('rng', rngs)
    @pytest.mark.parametrize('size_in, size_out', sizes)
    def test_RVS(self, rng, size_in, size_out):
        dist = StandardNormal()
        fni = stats.NumericalInverseHermite(dist)

        rng2 = deepcopy(rng)
        rvs = fni.rvs(size=size_in, random_state=rng)
        if size_in is not None:
            assert(rvs.shape == size_out)

        if rng2 is not None:
            rng2 = check_random_state(rng2)
            uniform = rng2.uniform(size=size_in)
            rvs2 = stats.norm.ppf(uniform)
            assert_allclose(rvs, rvs2)

    if NumpyVersion(np.__version__) >= '1.18.0':
        qrngs = [None, stats.qmc.Sobol(1, seed=0), stats.qmc.Halton(3, seed=0)]
    else:
        qrngs = []
    # `size=None` should not add anything to the shape, `size=1` should
    sizes = [(None, tuple()), (1, (1,)), (4, (4,)),
             ((4,), (4,)), ((2, 4), (2, 4))]  # type: ignore
    # Neither `d=None` nor `d=1` should add anything to the shape
    ds = [(None, tuple()), (1, tuple()), (3, (3,))]

    @pytest.mark.parametrize('qrng', qrngs)
    @pytest.mark.parametrize('size_in, size_out', sizes)
    @pytest.mark.parametrize('d_in, d_out', ds)
    def test_QRVS(self, qrng, size_in, size_out, d_in, d_out):
        dist = StandardNormal()
        fni = stats.NumericalInverseHermite(dist)

        # If d and qrng.d are inconsistent, an error is raised
        if d_in is not None and qrng is not None and qrng.d != d_in:
            match = "`d` must be consistent with dimension of `qmc_engine`."
            with pytest.raises(ValueError, match=match):
                fni.qrvs(size_in, d=d_in, qmc_engine=qrng)
            return

        # Sometimes d is really determined by qrng
        if d_in is None and qrng is not None and qrng.d != 1:
            d_out = (qrng.d,)

        shape_expected = size_out + d_out

        qrng2 = deepcopy(qrng)
        qrvs = fni.qrvs(size=size_in, d=d_in, qmc_engine=qrng)
        if size_in is not None:
            assert(qrvs.shape == shape_expected)

        if qrng2 is not None:
            uniform = qrng2.random(np.prod(size_in) or 1)
            qrvs2 = stats.norm.ppf(uniform).reshape(shape_expected)
            assert_allclose(qrvs, qrvs2, atol=1e-12)

    def test_QRVS_size_tuple(self):
        # QMCEngine samples are always of shape (n, d). When `size` is a tuple,
        # we set `n = prod(size)` in the call to qmc_engine.random, transform
        # the sample, and reshape it to the final dimensions. When we reshape,
        # we need to be careful, because the _columns_ of the sample returned
        # by a QMCEngine are "independent"-ish, but the elements within the
        # columns are not. We need to make sure that this doesn't get mixed up
        # by reshaping: qrvs[..., i] should remain "independent"-ish of
        # qrvs[..., i+1], but the elements within qrvs[..., i] should be
        # transformed from the same low-discrepancy sequence.
        if NumpyVersion(np.__version__) <= '1.18.0':
            pytest.skip("QMC doesn't play well with old NumPy")

        dist = StandardNormal()
        fni = stats.NumericalInverseHermite(dist)

        size = (3, 4)
        d = 5
        qrng = stats.qmc.Halton(d, seed=0)
        qrng2 = stats.qmc.Halton(d, seed=0)

        uniform = qrng2.random(np.prod(size))

        qrvs = fni.qrvs(size=size, d=d, qmc_engine=qrng)
        qrvs2 = stats.norm.ppf(uniform)

        for i in range(d):
            sample = qrvs[..., i]
            sample2 = qrvs2[:, i].reshape(size)
            assert_allclose(sample, sample2, atol=1e-12)

    def test_inaccurate_CDF(self):
        # CDF function with inaccurate tail cannot be inverted; see gh-13319
        # https://github.com/scipy/scipy/pull/13319#discussion_r626188955
        shapes = (2.3098496451481823, 0.6268795430096368)
        match = ("98 : one or more intervals very short; possibly due to "
                 "numerical problems with a pole or very flat tail")

        # fails with default tol
        with pytest.warns(RuntimeWarning, match=match):
            stats.NumericalInverseHermite(stats.beta(*shapes))

        # no error with coarser tol
        stats.NumericalInverseHermite(stats.beta(*shapes), u_resolution=1e-8)

    def test_custom_distribution(self):
        dist1 = StandardNormal()
        fni1 = stats.NumericalInverseHermite(dist1)

        dist2 = stats.norm()
        fni2 = stats.NumericalInverseHermite(dist2)

        assert_allclose(fni1.rvs(random_state=0), fni2.rvs(random_state=0))

    u = [
        # check the correctness of the PPF for equidistant points between
        # 0.02 and 0.98.
        np.linspace(0., 1., num=10000),
        # test the PPF method for empty arrays
        [], [[]],
        # test if nans and infs return nan result.
        [np.nan], [-np.inf, np.nan, np.inf],
        # test if a scalar is returned for a scalar input.
        0,
        # test for arrays with nans, values greater than 1 and less than 0,
        # and some valid values.
        [[np.nan, 0.5, 0.1], [0.2, 0.4, np.inf], [-2, 3, 4]]
    ]

    @pytest.mark.parametrize("u", u)
    def test_ppf(self, u):
        dist = StandardNormal()
        rng = NumericalInverseHermite(dist, u_resolution=1e-12)
        # Older versions of NumPy throw RuntimeWarnings for comparisons
        # with nan.
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "invalid value encountered in greater")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "greater_equal")
            sup.filter(RuntimeWarning, "invalid value encountered in less")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "less_equal")
            res = rng.ppf(u)
            expected = stats.norm.ppf(u)
        assert_allclose(res, expected, rtol=1e-9, atol=3e-10)
        assert res.shape == expected.shape

    def test_u_error(self):
        dist = StandardNormal()
        rng = NumericalInverseHermite(dist, u_resolution=1e-10)
        max_error, mae = rng.u_error()
        assert max_error < 1e-10
        assert mae <= max_error
        with suppress_warnings() as sup:
            # ignore warning about u-resolution being too small.
            sup.filter(RuntimeWarning)
            rng = NumericalInverseHermite(dist, u_resolution=1e-14)
        max_error, mae = rng.u_error()
        assert max_error < 1e-14
        assert mae <= max_error

    def test_deprecations(self):
        msg = ("`tol` has been deprecated and replaced with `u_resolution`. "
               "It will be completely removed in a future release.")
        with pytest.warns(DeprecationWarning, match=msg):
            NumericalInverseHermite(StandardNormal(), tol=1e-12)


class TestDiscreteGuideTable:
    basic_fail_dists = {
        'nchypergeom_fisher',  # numerical errors on tails
        'nchypergeom_wallenius',  # numerical errors on tails
        'randint'  # fails on 32-bit ubuntu
    }

    def test_guide_factor_gt3_raises_warning(self):
        pv = [0.1, 0.3, 0.6]
        urng = np.random.default_rng()
        with pytest.warns(RuntimeWarning):
            DiscreteGuideTable(pv, random_state=urng, guide_factor=7)

    def test_guide_factor_zero_raises_warning(self):
        pv = [0.1, 0.3, 0.6]
        urng = np.random.default_rng()
        with pytest.warns(RuntimeWarning):
            DiscreteGuideTable(pv, random_state=urng, guide_factor=0)

    def test_negative_guide_factor_raises_warning(self):
        # This occurs from the UNU.RAN wrapper automatically.
        # however it already gives a useful warning
        # Here we just test that a warning is raised.
        pv = [0.1, 0.3, 0.6]
        urng = np.random.default_rng()
        with pytest.warns(RuntimeWarning):
            DiscreteGuideTable(pv, random_state=urng, guide_factor=-1)

    @pytest.mark.parametrize("distname, params", distdiscrete)
    def test_basic(self, distname, params):
        if distname in self.basic_fail_dists:
            msg = ("DGT fails on these probably because of large domains "
                   "and small computation errors in PMF.")
            pytest.skip(msg)

        if not isinstance(distname, str):
            dist = distname
        else:
            dist = getattr(stats, distname)

        dist = dist(*params)
        domain = dist.support()

        if not np.isfinite(domain[1] - domain[0]):
            # DGT only works with finite domain. So, skip the distributions
            # with infinite tails.
            pytest.skip("DGT only works with a finite domain.")

        k = np.arange(domain[0], domain[1]+1)
        pv = dist.pmf(k)
        mv_ex = dist.stats('mv')
        rng = DiscreteGuideTable(dist, random_state=42)
        check_discr_samples(rng, pv, mv_ex)

    u = [
        # the correctness of the PPF for equidistant points between 0 and 1.
        np.linspace(0, 1, num=10000),
        # test the PPF method for empty arrays
        [], [[]],
        # test if nans and infs return nan result.
        [np.nan], [-np.inf, np.nan, np.inf],
        # test if a scalar is returned for a scalar input.
        0,
        # test for arrays with nans, values greater than 1 and less than 0,
        # and some valid values.
        [[np.nan, 0.5, 0.1], [0.2, 0.4, np.inf], [-2, 3, 4]]
    ]

    @pytest.mark.parametrize('u', u)
    def test_ppf(self, u):
        n, p = 4, 0.1
        dist = stats.binom(n, p)
        rng = DiscreteGuideTable(dist, random_state=42)

        # Older versions of NumPy throw RuntimeWarnings for comparisons
        # with nan.
        with suppress_warnings() as sup:
            sup.filter(RuntimeWarning, "invalid value encountered in greater")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "greater_equal")
            sup.filter(RuntimeWarning, "invalid value encountered in less")
            sup.filter(RuntimeWarning, "invalid value encountered in "
                                       "less_equal")

            res = rng.ppf(u)
            expected = stats.binom.ppf(u, n, p)
        assert_equal(res.shape, expected.shape)
        assert_equal(res, expected)

    @pytest.mark.parametrize("pv, msg", bad_pv_common)
    def test_bad_pv(self, pv, msg):
        with pytest.raises(ValueError, match=msg):
            DiscreteGuideTable(pv)

    # DGT doesn't support infinite tails. So, it should throw an error when
    # inf is present in the domain.
    inf_domain = [(-np.inf, np.inf), (np.inf, np.inf), (-np.inf, -np.inf),
                  (0, np.inf), (-np.inf, 0)]

    @pytest.mark.parametrize("domain", inf_domain)
    def test_inf_domain(self, domain):
        with pytest.raises(ValueError, match=r"must be finite"):
            DiscreteGuideTable(stats.binom(10, 0.2), domain=domain)


class Gamma:
    def __init__(self, p):
        self.p = p

    def pdf(self, x):
        return x**(self.p - 1) * math.exp(-x)

    def cdf(self, x):
        return stats.gamma.cdf(x, self.p)

    @staticmethod
    def support():
        return 0, np.inf


class TestNaiveRatioUniforms:

    def test_rv_generation_default(self):
        # use default settings (r=1, center=0)
        # exponential distribution
        class Exponential():
            def pdf(self, x):
                return np.exp(-x)

            def cdf(self, x):
                return 1 - np.exp(-x)

        dist = Exponential()
        rng = NaiveRatioUniforms(dist, v_max=1, u_min=0, u_max=2*np.exp(-1),
                                 random_state=76525)

        check_cont_samples(rng, dist, (1, 1))

    @pytest.mark.parametrize("r", [0.36, 1.0, 1.43])
    def test_rv_generation_r(self, r):
        # test different values of r
        # note: u_max, u_min are attained at the points -/+ sqrt((r+1)/r)
        dist = StandardNormal()
        y = np.sqrt((r+1)/r)
        u = (dist.pdf(y))**(r/(r+1)) * y
        v_max = (dist.pdf(0))**(1/(r+1))
        rng = NaiveRatioUniforms(dist, r=r, u_min=-u, u_max=u, v_max=v_max,
                                 random_state=7303)
        check_cont_samples(rng, dist, (0, 1))

    @pytest.mark.parametrize("p", [1.5, 2.83])
    def test_rv_generation_mode_shift(self, p):
        # test mode shift with Gamma(p) distribution, mode=p-1
        # note: the pdf is bounded only if p >= 1
        # note: u_max, u_min can be computed explicitly
        def u_bound(x, p, m):
            if x < 0:
                return 0
            return (x - m) * x**((p-1)/2) * math.exp(-x/2)
        dist = Gamma(p)
        m = p-1
        h = (p+1+m)/2
        k = np.sqrt(h**2 - m*(p-1))
        u_min, u_max = u_bound(h-k, p, m), u_bound(h+k, p, m)
        v_max = np.sqrt(dist.pdf(m))
        rng = NaiveRatioUniforms(dist, center=m, u_min=u_min, u_max=u_max,
                                 v_max=v_max, random_state=357)
        check_cont_samples(rng, dist, (p, p))

    def test_setup_no_bounds(self):
        dist = StandardNormal()
        u_bound = np.exp(-0.5) * np.sqrt(2)
        v_max = 1.0

        # no bound
        rng = NaiveRatioUniforms(dist, random_state=7303)
        check_cont_samples(rng, dist, (0, 1))
        # just v_max
        rng = NaiveRatioUniforms(dist, v_max=v_max, random_state=7303)
        check_cont_samples(rng, dist, (0, 1))
        # just one u-bound is missing
        msg = 'Only one of the values of u_min and u_max is None.'
        with pytest.warns(RuntimeWarning, match=msg):
            rng = NaiveRatioUniforms(dist, u_max=u_bound, random_state=7303)
            check_cont_samples(rng, dist, (0, 1))

    def test_exceptions(self):
        dist = StandardNormal()
        # need u_min < u_max
        msg = r"`u_min` must be smaller than `u_max`."
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, v_max=1, u_min=3, u_max=1)
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, v_max=1, u_min=1, u_max=1)
        # need v_max > 0
        msg = r'`v_max` must be positive.'
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, v_max=-1, u_min=1, u_max=3)
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, v_max=0, u_min=1, u_max=3)
        # need r > 0
        msg = r'`r` must be positive.'
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, r=0, v_max=1, u_min=1, u_max=3)
        with pytest.raises(ValueError, match=msg):
            NaiveRatioUniforms(dist, r=-0.24, v_max=1, u_min=1, u_max=3)