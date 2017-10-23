#ifndef HitStructures_H
#define HitStructures_H

#include "../Config.h"
#include "Hit.h"
#include "Track.h"
#include "TrackerInfo.h"
//#define DEBUG
#include "Debug.h"

#include <array>
#include <tbb/tbb.h>

typedef tbb::concurrent_vector<TripletIdx> TripletIdxConVec;

// for each layer
//   Config::nEtaBin vectors of hits, resized to large enough N
//   filled with corresponding hits
//   sorted in phi
//   and with a corresponding BinInfoSomething
//
// My gut feeling is that we could have them all in one place and just store
// indices into small enough eta-phi bins (oh, and eta is a horrible separation variable)
// 10*50000*64/1024/1024
//    30.51757812500000000000
// Or make them smaller ... if we could use short indices that might make a difference.
//
// Need to add eta/phi ... or something significantly better to the Hit struct
//
// At least ... fast eta/phi estimators.
//
// Oh, and we should use radix sort. Could one vectorize this?

// Need a good "array of pods" class with aligned alloc and automatic growth.
// For now just implement the no-resize / no-destroy basics in the BoH.

typedef std::pair<int, int> PhiBinInfo_t;

typedef std::vector<PhiBinInfo_t> vecPhiBinInfo_t;

typedef std::vector<vecPhiBinInfo_t> vecvecPhiBinInfo_t;

//==============================================================================

inline bool sortHitsByPhiMT(const Hit& h1, const Hit& h2)
{
  return std::atan2(h1.position()[1],h1.position()[0])<std::atan2(h2.position()[1],h2.position()[0]);
}

inline bool sortTrksByPhiMT(const Track& t1, const Track& t2)
{
  return t1.momPhi() < t2.momPhi();
}

//==============================================================================
//==============================================================================

// Use extra arrays to store phi and q of hits.
// MT: This would in principle allow fast selection of good hits, if
// we had good error estimates and reasonable *minimal* phi and q windows.
// Speed-wise, those arrays (filling AND access, about half each) cost 1.5%
// and could help us reduce the number of hits we need to process with bigger
// potential gains.

#define LOH_USE_PHI_Q_ARRAYS

// Note: the same code is used for barrel and endcap. In barrel the longitudinal
// bins are in Z and in endcap they are in R -- here this coordinate is called Q

class LayerOfHits
{
public:
  const LayerInfo          *m_layer_info = 0;
  Hit                      *m_hits = 0;
  vecvecPhiBinInfo_t        m_phi_bin_infos;
#ifdef LOH_USE_PHI_Q_ARRAYS
  std::vector<float>        m_hit_phis;
  std::vector<float>        m_hit_qs;
#endif

  float m_qmin, m_qmax, m_fq;
  int   m_nq = 0;
  int   m_capacity = 0;

  int   layer_id()  const { return m_layer_info->m_layer_id;    }
  bool  is_barrel() const { return m_layer_info->is_barrel();   }
  bool  is_endcap() const { return ! m_layer_info->is_barrel(); }

  bool  is_within_z_limits(float z) const { return m_layer_info->is_within_z_limits(z); }
  bool  is_within_r_limits(float r) const { return m_layer_info->is_within_r_limits(r); }

  WSR_Result is_within_z_sensitive_region(float z, float dz) const
  { return m_layer_info->is_within_z_sensitive_region(z, dz); }

  WSR_Result is_within_r_sensitive_region(float r, float dr) const
  { return m_layer_info->is_within_r_sensitive_region(r, dr); }

  float min_dphi() const { return m_layer_info->m_select_min_dphi; }
  float max_dphi() const { return m_layer_info->m_select_max_dphi; }
  float min_dq()   const { return m_layer_info->m_select_min_dq;   }
  float max_dq()   const { return m_layer_info->m_select_max_dq;   }

  bool  is_in_xy_hole(float x, float y) const { return m_layer_info->is_in_xy_hole(x, y); }

  // Testing bin filling
  static constexpr float m_fphi     = Config::m_nphi / Config::TwoPI;
  static constexpr int   m_phi_mask = 0x3ff;

protected:

  void setup_bins(float qmin, float qmax, float dq);

  void alloc_hits(int size)
  {
    m_hits = (Hit*) _mm_malloc(sizeof(Hit) * size, 64);
    m_capacity = size;
    for (int ihit = 0; ihit < m_capacity; ihit++){m_hits[ihit] = Hit();} 
#ifdef LOH_USE_PHI_Q_ARRAYS
    m_hit_phis.resize(size);
    m_hit_qs  .resize(size);
#endif
  }

  void free_hits()
  {
    _mm_free(m_hits);
  }

  void set_phi_bin(int q_bin, int phi_bin, int &hit_count, int &hits_in_bin)
  {
    m_phi_bin_infos[q_bin][phi_bin] = { hit_count, hit_count + hits_in_bin };
    hit_count  += hits_in_bin;
    hits_in_bin = 0;
  }

  void empty_phi_bins(int q_bin, int phi_bin_1, int phi_bin_2, int hit_count)
  {
    for (int pb = phi_bin_1; pb < phi_bin_2; ++pb)
    {
      m_phi_bin_infos[q_bin][pb] = { hit_count, hit_count };
    }
  }

  void empty_q_bins(int q_bin_1, int q_bin_2, int hit_count)
  {
    for (int qb = q_bin_1; qb < q_bin_2; ++qb)
    {
      empty_phi_bins(qb, 0, Config::m_nphi, hit_count);
    }
  }

public:
  LayerOfHits() {}

  ~LayerOfHits()
  {
    free_hits();
  }

  void  SetupLayer(const LayerInfo &li);

  void  Reset() {}

  float NormalizeQ(float q) const { if (q < m_qmin) return m_qmin; if (q > m_qmax) return m_qmax; return q; }

  int   GetQBin(float q)    const { return (q - m_qmin) * m_fq; }

  int   GetQBinChecked(float q) const { int qb = GetQBin(q); if (qb < 0) qb = 0; else if (qb >= m_nq) qb = m_nq - 1; return qb; }

  // if you don't pass phi in (-pi, +pi), mask away the upper bits using m_phi_mask
  int   GetPhiBin(float phi) const { return std::floor(m_fphi * (phi + Config::PI)); }

  const vecPhiBinInfo_t& GetVecPhiBinInfo(float q) const { return m_phi_bin_infos[GetQBin(q)]; }

  void  SuckInHits(const HitVec &hitv);

  void  SelectHitIndices(float q, float phi, float dq, float dphi, std::vector<int>& idcs, bool isForSeeding=false, bool dump=false);

  void  PrintBins();
};

//==============================================================================

class EventOfHits
{
public:
  std::vector<LayerOfHits> m_layers_of_hits;
  int                      m_n_layers;

public:
  EventOfHits(TrackerInfo &trk_inf);

  void Reset()
  {
    for (auto &i : m_layers_of_hits)
    {
      i.Reset();
    }
  }

  void SuckInHits(int layer, const HitVec &hitv)
  {
    m_layers_of_hits[layer].SuckInHits(hitv);
  }
};


//==============================================================================
//==============================================================================

// This should actually be a BunchOfCandidates that share common hit vector.
// At the moment this is an EtaBin ...

class EtaBinOfCandidates
{
public:
  std::vector<Track> m_candidates;

  int     m_capacity;
  int     m_size;

public:
  EtaBinOfCandidates() :
    m_candidates(Config::maxCandsPerEtaBin),
    m_capacity  (Config::maxCandsPerEtaBin),
    m_size      (0)
  {}

  void Reset()
  {
    m_size = 0;
  }

  void InsertTrack(const Track& track)
  {
    assert (m_size < m_capacity); // or something

    m_candidates[m_size] = track;
    ++m_size;
  }

  void SortByPhi()
  {
    std::sort(m_candidates.begin(), m_candidates.begin() + m_size, sortTrksByPhiMT);
  }
};

class EventOfCandidates
{
public:
  std::vector<EtaBinOfCandidates> m_etabins_of_candidates;

public:
  EventOfCandidates() :
    m_etabins_of_candidates(Config::nEtaBin)
  {}

  void Reset()
  {
    for (auto &i : m_etabins_of_candidates)
    {
      i.Reset();
    }
  }

  void InsertCandidate(const Track& track)
  {
    int bin = getEtaBinExtendedEdge(track.posEta());
    // XXXX MT Had to add back this conditional for best-hit (bad seeds)
    // Note also the ExtendedEdge above, this practically removes bin = -1
    // occurence and improves efficiency.
    if (bin != -1)
    {
      m_etabins_of_candidates[bin].InsertTrack(track);
    }
  }

  void SortByPhi()
  {
    for (auto &i : m_etabins_of_candidates)
    {
      i.SortByPhi();
    }
  }
};


//-------------------------------------------------------
// for combinatorial version, switch to vector of vectors
//-------------------------------------------------------

// This inheritance sucks but not doing it will require more changes.

class CombCandidate : public std::vector<Track>
{
public:
  enum SeedState_e { Dormant = 0, Finding, Finished };

  SeedState_e m_state           = Dormant;
  int         m_last_seed_layer = -1;
};

class EventOfCombCandidates
{
public:
  std::vector<CombCandidate> m_candidates;

  int     m_capacity;
  int     m_size;

public:
  EventOfCombCandidates(int size=0) :
    m_candidates(),
    m_capacity  (0),
    m_size      (0)
  {
    Reset(size);
  }

  void Reset(int new_capacity)
  {
    for (int s = 0; s < m_size; ++s)
    {
      m_candidates[s].clear();
    }

    if (new_capacity > m_capacity)
    {
      m_candidates.resize(new_capacity);

      for (int s = m_capacity; s < new_capacity; ++s)
      {
        m_candidates[s].reserve(Config::maxCandsPerSeed);//we should never exceed this
      }

      m_capacity = new_capacity;
    }

    m_size = 0;
  }

  CombCandidate& operator[](int i) { return m_candidates[i]; }

  void InsertSeed(const Track& seed)
  {
    assert (m_size < m_capacity);

    m_candidates[m_size].push_back(seed);
    m_candidates[m_size].m_state           = CombCandidate::Dormant;
    m_candidates[m_size].m_last_seed_layer = seed.getLastHitLyr();
    ++m_size;
  }

  void InsertTrack(const Track& track, int seed_index)
  {
    assert (seed_index < m_size);

    m_candidates[seed_index].push_back(track);
  }
};

#endif
