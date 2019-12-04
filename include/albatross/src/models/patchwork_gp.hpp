/*
 * Copyright (C) 2019 Swift Navigation Inc.
 * Contact: Swift Navigation <dev@swiftnav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef INCLUDE_ALBATROSS_MODELS_PATCHWORK_GP_H_
#define INCLUDE_ALBATROSS_MODELS_PATCHWORK_GP_H_

/*
 * Patchwork Gaussian Process is based off the paper:
 *
 *   Chiwoo Park and Daniel Apley. 2018. Patchwork Kriging for large-scale
 * Gaussian process regression. J. Mach. Learn. Res. 19, 1 (January 2018),
 * 269-311.
 *
 *   http://www.jmlr.org/papers/volume19/17-042/17-042.pdf
 *
 * The implementation requires that you define a class which contains three
 * methods:
 *
 * grouper :
 *
 *     GroupKey grouper(const FeatureType &f) const
 *
 *   This method should take a FeatureType and return which group it belongs to.
 *
 * boundary:
 *
 *     std::vector<BoundaryFeature> boundary(const GroupKey &x, const
 * GroupKey&y) const
 *
 *   This method should take two groups and return the features which represent
 * the boundary between two groups that will be constrained to be equal.
 *
 * nearest_group:
 *
 *   GroupKey nearest_group(const std::vector<GroupKey> &groups, const GroupKey
 * &query) const
 *
 *   This method is used during prediction and takes a vector of all the
 * available groups and a query group.  If the query exists it should simply
 * return the query.  But if the query group doesn't exist is should return the
 * nearest group.
 *
 */

namespace albatross {

namespace details {

DEFINE_CLASS_METHOD_TRAITS(boundary);

DEFINE_CLASS_METHOD_TRAITS(nearest_group);

DEFINE_CLASS_METHOD_TRAITS(grouper);

template <typename T, typename FeatureType>
class patchwork_functions_are_valid {

  using GroupKey = typename class_method_grouper_traits<
      T, typename const_ref<FeatureType>::type>::return_type;

  static constexpr bool has_valid_grouper = group_key_is_valid<GroupKey>::value;

  using ConstRefGroupKey = typename const_ref<GroupKey>::type;

  using BoundaryReturnType =
      typename class_method_boundary_traits<T, ConstRefGroupKey,
                                            ConstRefGroupKey>::return_type;

  static constexpr bool has_valid_boundary =
      is_vector<BoundaryReturnType>::value;

  using NearestGroupReturnType = typename class_method_nearest_group_traits<
      T, typename const_ref<std::vector<GroupKey>>::type,
      ConstRefGroupKey>::return_type;

  static constexpr bool has_valid_nearest_group =
      std::is_same<NearestGroupReturnType, GroupKey>::value;

public:
  static constexpr bool value =
      has_valid_grouper && has_valid_boundary && has_valid_nearest_group;
};

} // namespace details

/*
 * A BoundaryFeature represents a pseudo observation of the difference
 * between predictions from two different models.  In other words,
 *
 *   BoundaryFeature(key_i, key_j, feature)
 *
 * represents the quantity
 *
 *   model_i.predict(feature) - model_j.predict(feature)
 *
 * Patchwork Krigging uses these to force equivalence between two
 * otherwise independent models.
 */
template <typename GroupKey, typename FeatureType> struct BoundaryFeature {

  BoundaryFeature(const GroupKey &lhs_, const GroupKey &rhs_,
                  const FeatureType &feature_)
      : lhs(lhs_), rhs(rhs_), feature(feature_){};

  BoundaryFeature(GroupKey &&lhs_, GroupKey &&rhs_, FeatureType &&feature_)
      : lhs(std::move(lhs_)), rhs(std::move(rhs_)),
        feature(std::move(feature_)){};

  GroupKey lhs;
  GroupKey rhs;
  FeatureType feature;
};

template <typename GroupKey, typename FeatureType>
auto as_boundary_feature(GroupKey &&lhs, GroupKey &&rhs,
                         FeatureType &&feature) {
  using BoundaryFeatureType =
      BoundaryFeature<typename std::decay<GroupKey>::type,
                      typename std::decay<FeatureType>::type>;
  return BoundaryFeatureType(std::forward<GroupKey>(lhs),
                             std::forward<GroupKey>(rhs),
                             std::forward<FeatureType>(feature));
}

template <typename GroupKey, typename FeatureType>
auto as_boundary_features(GroupKey &&lhs, GroupKey &&rhs,
                          const std::vector<FeatureType> &features) {
  using BoundaryFeatureType =
      BoundaryFeature<typename std::decay<GroupKey>::type,
                      typename std::decay<FeatureType>::type>;

  std::vector<BoundaryFeatureType> boundary_features;
  for (const auto &f : features) {
    boundary_features.emplace_back(as_boundary_feature(lhs, rhs, f));
  }
  return boundary_features;
}

/*
 * GroupFeature
 */

template <typename GroupKey, typename FeatureType> struct GroupFeature {

  GroupFeature(const GroupKey &key_, const FeatureType &feature_)
      : key(key_), feature(feature_){};

  GroupFeature(GroupKey &&key_, FeatureType &&feature_)
      : key(std::move(key_)), feature(std::move(feature_)){};

  GroupKey key;
  FeatureType feature;
};

template <typename GroupKey, typename FeatureType>
auto as_group_feature(GroupKey &&key, FeatureType &&feature) {
  using GroupFeatureType = GroupFeature<typename std::decay<GroupKey>::type,
                                        typename std::decay<FeatureType>::type>;
  return GroupFeatureType(std::forward<GroupKey>(key),
                          std::forward<FeatureType>(feature));
}

template <typename GrouperFunction, typename FeatureType>
auto as_group_features(const GroupBy<std::vector<FeatureType>, GrouperFunction>
                           &grouped_features) {

  using GroupKey =
      typename GroupBy<std::vector<FeatureType>, GrouperFunction>::KeyType;
  using GroupFeatureType =
      GroupFeature<GroupKey, typename std::decay<FeatureType>::type>;

  std::vector<GroupFeatureType> group_features;

  auto emplace_in_output = [&](const auto &key, const auto &features) {
    for (const auto &f : features) {
      group_features.emplace_back(as_group_feature(key, f));
    }
  };
  grouped_features.apply(emplace_in_output);

  return group_features;
}

template <typename GroupKey, typename FeatureType>
auto as_group_features(const GroupKey &key,
                       const std::vector<FeatureType> &features) {
  using GroupFeatureType =
      GroupFeature<GroupKey, typename std::decay<FeatureType>::type>;

  std::vector<GroupFeatureType> group_features;
  for (const auto &f : features) {
    group_features.emplace_back(as_group_feature(key, f));
  }
  return group_features;
}

template <typename SubCaller> struct PatchworkCallerBase {
  template <
      typename CovFunc, typename X, typename Y,
      typename std::enable_if<
          has_valid_cov_caller<CovFunc, SubCaller, X, Y>::value, int>::type = 0>
  static double call(const CovFunc &cov_func, const X &x, const Y &y) {
    return SubCaller::call(cov_func, x, y);
  }

  template <typename CovFunc, typename GroupKey, typename FeatureTypeX,
            typename FeatureTypeY>
  static double call(const CovFunc &cov_func,
                     const GroupFeature<GroupKey, FeatureTypeX> &x,
                     const GroupFeature<GroupKey, FeatureTypeY> &y) {
    if (x.key == y.key) {
      return SubCaller::call(cov_func, x.feature, y.feature);
    } else {
      return 0.;
    }
  }

  template <typename CovFunc, typename GroupKey, typename FeatureTypeX,
            typename FeatureTypeY>
  static double call(const CovFunc &cov_func,
                     const GroupFeature<GroupKey, FeatureTypeX> &x,
                     const BoundaryFeature<GroupKey, FeatureTypeY> &y) {
    if (x.key == y.lhs) {
      return SubCaller::call(cov_func, x.feature, y.feature);
    } else if (x.key == y.rhs) {
      return -SubCaller::call(cov_func, x.feature, y.feature);
    } else {
      return 0.;
    }
  }

  template <typename CovFunc, typename GroupKey, typename FeatureTypeX,
            typename FeatureTypeY>
  static double call(const CovFunc &cov_func,
                     const BoundaryFeature<GroupKey, FeatureTypeX> &x,
                     const BoundaryFeature<GroupKey, FeatureTypeY> &y) {

    if (x.lhs == y.lhs && x.rhs == y.rhs) {
      return 2 * SubCaller::call(cov_func, x.feature, y.feature);
    } else if (x.lhs == y.lhs && x.rhs != y.rhs) {
      return SubCaller::call(cov_func, x.feature, y.feature);
    } else if (x.lhs != y.lhs && x.rhs == y.rhs) {
      return SubCaller::call(cov_func, x.feature, y.feature);
    } else if (x.lhs == y.rhs && x.rhs != y.lhs) {
      return -SubCaller::call(cov_func, x.feature, y.feature);
    } else if (x.lhs != y.rhs && x.rhs == y.lhs) {
      return -SubCaller::call(cov_func, x.feature, y.feature);
    } else {
      return 0.;
    }
  }
};

// The PatchworkCaller tries symmetric versions of the PatchworkCallerBase
// and otherwise resorts to the DefaultCaller
using PatchworkCaller =
    internal::SymmetricCaller<PatchworkCallerBase<DefaultCaller>>;

/*
 * Patchwork GP works by clustering all the data into groups which
 * results in several Grouped objects containing block matrix representations.
 *
 * These subsequent methods make those representations easier to work with.
 */
template <typename GroupKey, typename X, typename Y, typename ApplyFunction>
inline Eigen::MatrixXd block_accumulate(const Grouped<GroupKey, X> &lhs,
                                        const Grouped<GroupKey, Y> &rhs,
                                        const ApplyFunction &apply_function) {
  // block_accumulate takes two different grouped objects and returns
  // the sum of that function applied to each pair of values.  Another
  // way of writing this could be something like:
  //
  //   sum_i ( apply_function(lhs.at(key_i), rhs.at(key_i)) )
  //
  // The result of apply_function is expected to be an Eigen::MatrixXd
  static_assert(
      std::is_same<Eigen::MatrixXd,
                   typename invoke_result<ApplyFunction, X, Y>::type>::value,
      "apply_function needs to return an Eigen::MatrixXd type");

  assert(lhs.size() == rhs.size());
  assert(lhs.size() > 0);

  auto one_group = [&](const auto &key) {
    assert(map_contains(lhs, key) && map_contains(rhs, key));
    return apply_function(lhs.at(key), rhs.at(key));
  };

  const auto keys = lhs.keys();
  Eigen::MatrixXd output = one_group(keys[0]);
  for (std::size_t i = 1; i < keys.size(); ++i) {
    // Eigen internally asserts that the results are the same size.
    output += one_group(keys[i]);
  }

  return output;
}

template <typename GroupKey, typename ApplyFunction>
inline Eigen::MatrixXd
block_product(const Grouped<GroupKey, Eigen::MatrixXd> &lhs,
              const Grouped<GroupKey, Eigen::MatrixXd> &rhs) {
  // This performs a block matrix product operation where if you aligned the
  // lhs into horizontal blocks and the right into vertical blocks by ordering
  // their keys:
  //
  //   lhs = [x_0, ..., x_n]
  //   rhs = [y_0, ..., y_n]
  //
  // the output corresponds to:
  //
  //   lhs * rhs = [x_0, ..., x_2] * [y_0
  //                                  ...
  //                                  y_2]
  //
  auto matrix_product = [&](const auto &x, const auto &y) {
    return (x * y).eval();
  };

  return block_accumulate(lhs, rhs, matrix_product);
}

template <typename GroupKey>
inline Eigen::MatrixXd
block_inner_product(const Grouped<GroupKey, Eigen::MatrixXd> &lhs,
                    const Grouped<GroupKey, Eigen::MatrixXd> &rhs) {
  // This performs a block matrix inner product operation where if you aligned
  // the lhs into horizontal blocks and the right into vertical blocks by
  // ordering their keys:
  //
  //   lhs = [x_0, ..., x_n]
  //   rhs = [y_0, ..., y_n]
  //
  // the output corresponds to:
  //
  //   lhs.T * rhs = [x_0.T, ..., x_n.T] * [y_0
  //                                        ...
  //                                        y_n]
  //
  auto matrix_inner_product = [&](const auto &x, const auto &y) {
    return (x.transpose() * y).eval();
  };

  return block_accumulate(lhs, rhs, matrix_inner_product);
}

template <typename GroupKey, typename Solver, typename Rhs>
inline auto block_solve(const Grouped<GroupKey, Solver> &lhs,
                        const Grouped<GroupKey, Rhs> &rhs) {
  // Here we have the equivalent to a block diagonal matrix solve
  // in which the inverse of each group in the lhs is applied to
  // the corresponding group in rhs.
  //
  //   lhs = [x_0, ..., x_n]
  //   rhs = [y_0, ..., y_n]
  //
  // the output corresponds to:
  //
  //   lhs.T * rhs = [x_0^-1, ..., x_n^-1] * [y_0
  //                                          ...
  //                                          y_n]
  //
  const auto solve_one_block = [&](const auto &key, const auto &x) {
    return (lhs.at(key).solve(x)).eval();
  };

  return rhs.apply(solve_one_block);
};

template <typename FitModelType, typename GroupKey> struct PatchworkGPFit {};

template <typename ModelType, typename FitType, typename GroupKey>
struct Fit<PatchworkGPFit<FitModel<ModelType, FitType>, GroupKey>> {

  using FeatureType = typename FitType::Feature;

  Grouped<GroupKey, FitModel<ModelType, FitType>> fit_models;

  //  Eigen::MatrixXd C_bb_inv_C_bd;
  //  Grouped<GroupKey, Eigen::VectorXd> information;
  //  PatchworkFunctions patchwork_functions;
  //  using GroupKey = typename
  //  details::grouper_result<PatchworkFunctions::GrouperFunction,
  //                                                    FeatureType>::type;

  Fit(){};

  Fit(const Grouped<GroupKey, FitModel<ModelType, FitType>> &fit_models_)
      : fit_models(fit_models_){};

  Fit(const Grouped<GroupKey, FitModel<ModelType, FitType>> &&fit_models_)
      : fit_models(std::move(fit_models_)){};
};

template <typename BoundaryFunction, typename GroupKey>
auto build_boundary_features(const BoundaryFunction &boundary_function,
                             const std::vector<GroupKey> &groups) {
  using BoundarySubFeatureType =
      typename invoke_result<BoundaryFunction, GroupKey,
                             GroupKey>::type::value_type;
  using BoundaryFeatureType = BoundaryFeature<GroupKey, BoundarySubFeatureType>;

  std::vector<BoundaryFeatureType> boundary_features;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    for (std::size_t j = i + 1; j < groups.size(); ++j) {
      const auto next_boundary_features = as_boundary_features(
          groups[i], groups[j], boundary_function(groups[i], groups[j]));
      if (next_boundary_features.size() > 0) {
        boundary_features.insert(boundary_features.end(),
                                 next_boundary_features.begin(),
                                 next_boundary_features.end());
      }
    }
  }
  assert(boundary_features.size() > 0);
  return boundary_features;
}

template <typename CovFunc, typename PatchworkFunctions>
class PatchworkGaussianProcess
    : public GaussianProcessBase<
          CovFunc, PatchworkGaussianProcess<CovFunc, PatchworkFunctions>> {

public:
  using Base = GaussianProcessBase<
      CovFunc, PatchworkGaussianProcess<CovFunc, PatchworkFunctions>>;

  PatchworkGaussianProcess() : Base(){};
  PatchworkGaussianProcess(CovFunc &covariance_function)
      : Base(covariance_function){};
  PatchworkGaussianProcess(CovFunc &covariance_function,
                           PatchworkFunctions patchwork_functions)
      : Base(covariance_function), patchwork_functions_(patchwork_functions){};

  template <typename FitModelType, typename GroupKey>
  auto
  from_fit_models(const Grouped<GroupKey, FitModelType> &fit_models) const {
    using PatchworkFitType = Fit<PatchworkGPFit<FitModelType, GroupKey>>;
    return FitModel<PatchworkGaussianProcess, PatchworkFitType>(
        *this, PatchworkFitType(fit_models));
  };

  template <typename FeatureType, typename FitModelType, typename GroupKey>
  JointDistribution _predict_impl(
      const std::vector<FeatureType> &features,
      const Fit<PatchworkGPFit<FitModelType, GroupKey>> &patchwork_fit,
      PredictTypeIdentity<JointDistribution> &&) const {

    const auto fit_models = patchwork_fit.fit_models;

    if (fit_models.size() == 1) {
      return fit_models.values()[0].predict(features).joint();
    }

    auto get_obs_vector = [](const auto &fit_model) {
      // TOOD: should these be converted to Measurement<> types?
      return fit_model.predict(fit_model.get_fit().train_features).mean();
    };
    const auto obs_vectors = patchwork_fit.fit_models.apply(get_obs_vector);

    auto boundary_function = [&](const GroupKey &x, const GroupKey &y) {
      return patchwork_functions_.boundary(x, y);
    };

    const auto boundary_features =
        build_boundary_features(boundary_function, fit_models.keys());

    auto patchwork_caller = [&](const auto &x, const auto &y) {
      return PatchworkCaller::call(this->covariance_function_, x, y);
    };

    auto patchwork_covariance_matrix = [&](const auto &xs, const auto &ys) {
      return compute_covariance_matrix(patchwork_caller, xs, ys);
    };

    // C_bb is the covariance matrix between all boundaries, it will
    // have a lot of zeros so could be decomposed more efficiently
    const Eigen::MatrixXd C_bb =
        patchwork_covariance_matrix(boundary_features, boundary_features);
    const auto C_bb_ldlt = C_bb.ldlt();

    // C_dd is the large block diagonal matrix, with one block for each model
    // or which we already have an efficient way of computing the inverse.
    auto get_train_covariance = [](const auto &fit_model) {
      return fit_model.get_fit().train_covariance;
    };
    const auto C_dd = fit_models.apply(get_train_covariance);

    auto get_features = [](const auto &fit_model) {
      return fit_model.get_fit().train_features;
    };

    // C_db holds the covariance between each model and all boundaries.
    // The actual storage is effectively a map with values which correspond
    // to the covariance between that model's features and the boundaries.
    auto C_db_one_group = [&](const auto &key, const auto &fit_model) {
      const auto group_features =
          as_group_features(key, get_features(fit_model));
      return patchwork_covariance_matrix(group_features, boundary_features);
    };
    const auto C_db = fit_models.apply(C_db_one_group);
    const auto C_dd_inv_C_db = block_solve(C_dd, C_db);

    //    S_bb = C_bb - C_db * C_dd^-1 * C_db
    const Eigen::MatrixXd S_bb =
        C_bb - block_inner_product(C_db, C_dd_inv_C_db);
    const auto S_bb_ldlt = S_bb.ldlt();

    auto solver = [&](const auto &rhs) {
      // A^-1 rhs + A^-1 C (B - C^T A^-1 C)^-1 C^T A^-1 rhs
      // A^-1 rhs + A^-1 C S^-1 C^T A^-1 rhs

      // A = C_dd
      // B = C_bb
      // C = C_db
      // S = S_bb = (B - C^T A^-1 C)
      const auto Ai_rhs = block_solve(C_dd, rhs);

      // S_bb^-1 C^T A^-1 rhs
      auto SiCtAi_rhs_block = [&](const Eigen::MatrixXd &C_db_i,
                                  const auto &Ai_rhs_i) {
        return Eigen::MatrixXd(S_bb_ldlt.solve(C_db_i.transpose() * Ai_rhs_i));
      };
      const Eigen::MatrixXd SiCtAi_rhs =
          block_accumulate(C_db, Ai_rhs, SiCtAi_rhs_block);

      auto product_with_SiCtAi_rhs = [&](const auto &key, const auto &C_db_i) {
        return Eigen::MatrixXd(C_db_i * SiCtAi_rhs);
      };
      const auto CSiCtAi_rhs = C_db.apply(product_with_SiCtAi_rhs);

      auto output = block_solve(C_dd, CSiCtAi_rhs);
      // Adds A^-1 rhs to A^-1 C S^-1 C^T A^-1 rhs
      auto add_Ai_rhs = [&](const auto &key, const auto &group) {
        return (group + Ai_rhs.at(key)).eval();
      };
      return output.apply(add_Ai_rhs);
    };

    const auto ys = fit_models.apply(get_obs_vector);
    const auto information = solver(ys);

    Eigen::VectorXd C_bb_inv_C_bd_information =
        Eigen::VectorXd::Zero(C_bb.rows());
    auto accumulate_C_bb_inv_C_bd_information = [&](const auto &key,
                                                    const auto &C_bd_i) {
      C_bb_inv_C_bd_information +=
          C_bb_ldlt.solve(C_bd_i.transpose() * information.at(key));
    };
    C_db.apply(accumulate_C_bb_inv_C_bd_information);

    /*
     * PREDICT
     */

    auto predict_grouper = [&](const auto &f) {
      return patchwork_functions_.nearest_group(
          C_db.keys(), patchwork_functions_.grouper(f));
    };

    const auto grouped = group_by(features, predict_grouper);
    const auto group_features = as_group_features(grouped);

    const Eigen::MatrixXd C_fb =
        patchwork_covariance_matrix(group_features, boundary_features);
    const auto C_fb_bb_inv = C_bb_ldlt.solve(C_fb.transpose()).transpose();

    auto compute_cross_block_transpose = [&](const auto &key,
                                             const auto &fit_model) {
      const auto train_features =
          as_group_features(key, get_features(fit_model));
      Eigen::MatrixXd block =
          patchwork_covariance_matrix(train_features, group_features);
      block -= C_db.at(key) * C_fb_bb_inv.transpose();
      return block;
    };

    const auto cross_transpose =
        fit_models.apply(compute_cross_block_transpose);
    const auto C_dd_inv_cross = solver(cross_transpose);

    const Eigen::VectorXd mean =
        block_inner_product(cross_transpose, information);
    const Eigen::MatrixXd explained =
        block_inner_product(cross_transpose, C_dd_inv_cross);

    const Eigen::MatrixXd cov =
        patchwork_covariance_matrix(group_features, group_features) -
        C_fb_bb_inv * C_fb.transpose() - explained;

    return JointDistribution(mean, cov);
  }

  template <typename FeatureType>
  auto _fit_impl(const std::vector<FeatureType> &features,
                 const MarginalDistribution &targets) const {

    static_assert(details::patchwork_functions_are_valid<PatchworkFunctions,
                                                         FeatureType>::value,
                  "Invalid PatchworkFunctions for this FeatureType");

    const auto m = gp_from_covariance(this->covariance_function_, "internal");

    auto create_fit_model = [&](const auto &dataset) { return m.fit(dataset); };

    const RegressionDataset<FeatureType> dataset(features, targets);

    auto grouper = [&](const auto &f) {
      return patchwork_functions_.grouper(f);
    };

    const auto fit_models = dataset.group_by(grouper).apply(create_fit_model);

    return from_fit_models(fit_models).get_fit();
  }

  struct PatchworkFunctionsWithMeasurement {

    PatchworkFunctionsWithMeasurement(PatchworkFunctions functions)
        : functions_(functions) {}

    template <typename X> auto grouper(const X &x) const {
      return functions_.grouper(x);
    }

    template <typename X> auto grouper(const Measurement<X> &x) const {
      return functions_.grouper(x.value);
    }

    template <typename GroupKey>
    auto boundary(const GroupKey &x, const GroupKey &y) const {
      return functions_.boundary(x, y);
    }

    template <typename GroupKey>
    auto nearest_group(const std::vector<GroupKey> &groups,
                       const GroupKey &query) const {
      return functions_.nearest_group(groups, query);
    }

    PatchworkFunctions functions_;
  };

  PatchworkFunctionsWithMeasurement patchwork_functions_;
};

template <typename CovFunc, typename PatchworkFunctions>
inline PatchworkGaussianProcess<CovFunc, PatchworkFunctions>
patchwork_gp_from_covariance(CovFunc covariance_function,
                             PatchworkFunctions patchwork_functions) {
  return PatchworkGaussianProcess<CovFunc, PatchworkFunctions>(
      covariance_function, patchwork_functions);
};

} // namespace albatross

#endif /* INCLUDE_ALBATROSS_MODELS_SPARSE_GP_H_ */