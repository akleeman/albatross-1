/*
 * Copyright (C) 2018 Swift Navigation Inc.
 * Contact: Swift Navigation <dev@swiftnav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef ALBATROSS_CORE_INDEXING_H
#define ALBATROSS_CORE_INDEXING_H

#include "core/dataset.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>

namespace albatross {

using FoldIndices = std::vector<std::size_t>;
using FoldName = std::string;
using FoldIndexer = std::map<FoldName, FoldIndices>;

template <typename FeatureType>
using IndexerFunction =
    std::function<FoldIndexer(const RegressionDataset<FeatureType> &)>;

/*
 * Extract a subset of a standard vector.
 */
template <typename SizeType, typename X>
inline std::vector<X> subset(const std::vector<SizeType> &indices,
                             const std::vector<X> &v) {
  std::vector<X> out(indices.size());
  for (std::size_t i = 0; i < static_cast<std::size_t>(indices.size()); i++) {
    out[i] = v[static_cast<std::size_t>(indices[i])];
  }
  return out;
}

/*
 * Extract a subset of an Eigen::Vector
 */
template <typename SizeType>
inline Eigen::VectorXd subset(const std::vector<SizeType> &indices,
                              const Eigen::VectorXd &v) {
  Eigen::VectorXd out(static_cast<Eigen::Index>(indices.size()));
  for (std::size_t i = 0; i < indices.size(); i++) {
    out[static_cast<Eigen::Index>(i)] =
        v[static_cast<Eigen::Index>(indices[i])];
  }
  return out;
}

/*
 * Convenience method which subsets the features and targets of a dataset.
 */
template <typename SizeType, typename FeatureType>
inline RegressionDataset<FeatureType>
subset(const std::vector<SizeType> &indices,
       const RegressionDataset<FeatureType> &dataset) {
  return RegressionDataset<FeatureType>(subset(indices, dataset.features),
                                        subset(indices, dataset.targets),
                                        dataset.metadata);
}

/*
 * Extracts a subset of columns from an Eigen::Matrix
 */
template <typename SizeType>
inline Eigen::MatrixXd subset_cols(const std::vector<SizeType> &col_indices,
                                   const Eigen::MatrixXd &v) {
  Eigen::MatrixXd out(v.rows(), col_indices.size());
  for (std::size_t i = 0; i < col_indices.size(); i++) {
    auto ii = static_cast<Eigen::Index>(i);
    auto col_index = static_cast<Eigen::Index>(col_indices[i]);
    out.col(ii) = v.col(col_index);
  }
  return out;
}

/*
 * Extracts a subset of an Eigen::Matrix for the given row and column
 * indices.
 */
template <typename SizeType>
inline Eigen::MatrixXd subset(const std::vector<SizeType> &row_indices,
                              const std::vector<SizeType> &col_indices,
                              const Eigen::MatrixXd &v) {
  Eigen::MatrixXd out(row_indices.size(), col_indices.size());
  for (std::size_t i = 0; i < row_indices.size(); i++) {
    for (std::size_t j = 0; j < col_indices.size(); j++) {
      auto ii = static_cast<Eigen::Index>(i);
      auto jj = static_cast<Eigen::Index>(j);
      auto row_index = static_cast<Eigen::Index>(row_indices[i]);
      auto col_index = static_cast<Eigen::Index>(col_indices[j]);
      out(ii, jj) = v(row_index, col_index);
    }
  }
  return out;
}

/*
 * Takes a symmetric subset of an Eigen::Matrix.  Ie, it'll index the same rows
 * and
 * columns.
 */
template <typename SizeType>
inline Eigen::MatrixXd symmetric_subset(const std::vector<SizeType> &indices,
                                        const Eigen::MatrixXd &v) {
  assert(v.rows() == v.cols());
  return subset(indices, indices, v);
}

/*
 * Extract a subset of an Eigen::DiagonalMatrix
 */
template <typename SizeType, typename Scalar, int Size>
inline Eigen::DiagonalMatrix<Scalar, Size>
symmetric_subset(const std::vector<SizeType> &indices,
                 const Eigen::DiagonalMatrix<Scalar, Size> &v) {
  return subset(indices, v.diagonal()).asDiagonal();
}

template <typename CovarianceType, typename SizeType>
Distribution<CovarianceType> subset(const std::vector<SizeType> &indices,
                                    const Distribution<CovarianceType> &dist) {
  auto mean = subset(indices, Eigen::VectorXd(dist.mean));
  if (dist.has_covariance()) {
    auto cov = symmetric_subset(indices, dist.covariance);
    return Distribution<CovarianceType>(mean, cov);
  } else {
    return Distribution<CovarianceType>(mean);
  }
}

/*
 * A combination of training and testing datasets, typically used in cross
 * validation.
 */
template <typename FeatureType> struct RegressionFold {
  RegressionDataset<FeatureType> train_dataset;
  RegressionDataset<FeatureType> test_dataset;
  FoldName name;
  FoldIndices test_indices;

  RegressionFold(const RegressionDataset<FeatureType> &train_dataset_,
                 const RegressionDataset<FeatureType> &test_dataset_,
                 const FoldName &name_, const FoldIndices &test_indices_)
      : train_dataset(train_dataset_), test_dataset(test_dataset_), name(name_),
        test_indices(test_indices_){};
};

template <typename X>
inline std::vector<X> vector_set_difference(const std::vector<X> &x,
                                            const std::vector<X> &y) {
  std::vector<X> diff;
  std::set_difference(x.begin(), x.end(), y.begin(), y.end(),
                      std::inserter(diff, diff.begin()));
  return diff;
}

/*
 * Computes the indices between 0 and n - 1 which are NOT contained
 * in `indices`.  Here complement is the mathematical interpretation
 * of the word meaning "the part required to make something whole".
 * In other words, indices and indices_complement(indices) should
 * contain all the numbers between 0 and n-1
 */
inline FoldIndices indices_complement(const FoldIndices &indices, const int n) {
  FoldIndices all_indices(n);
  std::iota(all_indices.begin(), all_indices.end(), 0);
  return vector_set_difference(all_indices, indices);
}

/*
 * Each flavor of cross validation can be described by a set of
 * FoldIndices, which store which indices should be used for the
 * test cases.  This function takes a map from FoldName to
 * FoldIndices and a dataset and creates the resulting folds.
 */
template <typename FeatureType>
static inline std::vector<RegressionFold<FeatureType>>
folds_from_fold_indexer(const RegressionDataset<FeatureType> &dataset,
                        const FoldIndexer &groups) {
  // For a dataset with n features, we'll have n folds.
  const std::size_t n = dataset.features.size();
  std::vector<RegressionFold<FeatureType>> folds;
  // For each fold, partition into train and test sets.
  for (const auto &pair : groups) {
    // These get exposed inside the returned RegressionFold and because
    // we'd like to prevent modification of the output from this function
    // from changing the input FoldIndexer we perform a copy here.
    const FoldName group_name(pair.first);
    const FoldIndices test_indices(pair.second);
    const auto train_indices = indices_complement(test_indices, n);

    std::vector<FeatureType> train_features =
        subset(train_indices, dataset.features);
    MarginalDistribution train_targets = subset(train_indices, dataset.targets);

    std::vector<FeatureType> test_features =
        subset(test_indices, dataset.features);
    MarginalDistribution test_targets = subset(test_indices, dataset.targets);

    assert(train_features.size() == train_targets.size());
    assert(test_features.size() == test_targets.size());
    assert(test_targets.size() + train_targets.size() == n);

    const RegressionDataset<FeatureType> train_split(train_features,
                                                     train_targets);
    const RegressionDataset<FeatureType> test_split(test_features,
                                                    test_targets);
    folds.push_back(RegressionFold<FeatureType>(train_split, test_split,
                                                group_name, test_indices));
  }
  return folds;
}

template <typename FeatureType>
static inline FoldIndexer
leave_one_out_indexer(const RegressionDataset<FeatureType> &dataset) {
  FoldIndexer groups;
  for (std::size_t i = 0; i < dataset.features.size(); i++) {
    FoldName group_name = std::to_string(i);
    groups[group_name] = {i};
  }
  return groups;
}

/*
 * Splits a dataset into cross validation folds where each fold contains all but
 * one predictor/target pair.
 */
template <typename FeatureType>
static inline FoldIndexer leave_one_group_out_indexer(
    const std::vector<FeatureType> &features,
    const std::function<FoldName(const FeatureType &)> &get_group_name) {
  FoldIndexer groups;
  for (std::size_t i = 0; i < features.size(); i++) {
    const std::string k = get_group_name(features[i]);
    // Get the existing indices if we've already encountered this group_name
    // otherwise initialize a new one.
    FoldIndices indices;
    if (groups.find(k) == groups.end()) {
      indices = FoldIndices();
    } else {
      indices = groups[k];
    }
    // Add the current index.
    indices.push_back(i);
    groups[k] = indices;
  }
  return groups;
}

/*
 * Splits a dataset into cross validation folds where each fold contains all but
 * one predictor/target pair.
 */
template <typename FeatureType>
static inline FoldIndexer leave_one_group_out_indexer(
    const RegressionDataset<FeatureType> &dataset,
    const std::function<FoldName(const FeatureType &)> &get_group_name) {
  return leave_one_group_out_indexer(dataset.features, get_group_name);
}

/*
 * Generates cross validation folds which represent leave one out
 * cross validation.
 */
template <typename FeatureType>
static inline std::vector<RegressionFold<FeatureType>>
leave_one_out(const RegressionDataset<FeatureType> &dataset) {
  return folds_from_fold_indexer<FeatureType>(
      dataset, leave_one_out_indexer<FeatureType>(dataset));
}

/*
 * Uses a `get_group_name` function to bucket each FeatureType into
 * a group, then holds out one group at a time.
 */
template <typename FeatureType>
static inline std::vector<RegressionFold<FeatureType>> leave_one_group_out(
    const RegressionDataset<FeatureType> &dataset,
    const std::function<FoldName(const FeatureType &)> &get_group_name) {
  const FoldIndexer indexer =
      leave_one_group_out_indexer<FeatureType>(dataset, get_group_name);
  return folds_from_fold_indexer<FeatureType>(dataset, indexer);
}

} // namespace albatross

#endif
