/* 
 *  libpinyin
 *  Library to deal with pinyin.
 *  
 *  Copyright (C) 2017 Peng Wu <alexepico@gmail.com>
 *  
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PHONETIC_LOOKUP_H
#define PHONETIC_LOOKUP_H


#include "novel_types.h"
#include <limits.h>
#include <math.h>
#include "phonetic_key_matrix.h"
#include "ngram.h"

namespace pinyin{

struct trellis_value_t {
    phrase_token_t m_handles[2];
    // the character length of the final sentence.
    gint32 m_sentence_length;
    gfloat m_poss;
    // the m_last_step and m_sub_index points to this trellis.
    gint32 m_last_step;
    // the m_sub_index points to the inside of last trellis_node.
    gint32 m_sub_index;
    // the current index in this trellis_node.
    // only initialized in the get_candidates method.
    gint32 m_current_index;

    trellis_value_t(gfloat poss = FLT_MAX){
        m_handles[0] = null_token;
        m_handles[1] = null_token;
        m_sentence_length = 0;
        m_poss = poss;
        m_last_step = -1;
        m_sub_index = -1;
        m_current_index = -1;
    }
};

template <gint32 nbest>
static bool inline trellis_value_less_than(const trellis_value_t * exist_item,
                                           const trellis_value_t * new_item) {
    /* shorter sentence */
    if (exist_item->m_sentence_length > new_item->m_sentence_length ||
        /* the same length but better possibility */
        (exist_item->m_sentence_length == new_item->m_sentence_length &&
         exist_item->m_poss < new_item->m_poss))
        return true;

    if (nbest > 1) {
        /* allow longer sentence */
        if (exist_item->m_current_index == 0 &&
            exist_item->m_sentence_length == new_item->m_sentence_length + 1 &&
            exist_item->m_poss < new_item->m_poss)
            return true;
    }

    return false;
}

#if 0
struct matrix_value_t {
    phrase_token_t m_cur_token;
    /* just propagate the value from tail matrix step. */
    gfloat m_poss;
    // the below information for recovering the final phrase array.
    // the m_next_step and m_next_index points to this matrix.
    gint32 m_next_step;
    // the m_next_index points to the last matrix_step.
    gint32 m_next_index;

    matrix_value_t(){
        m_cur_token = null_token;
        m_poss = FLT_MAX;
        m_next_step = -1;
        m_next_index = -1;
    }
};
#endif

#if 1
/* for debug purpose */
#include "phonetic_lookup_linear.h"
#else
/* for optimization */
#include "phonetic_lookup_heap.h"
#endif

enum constraint_type{NO_CONSTRAINT, CONSTRAINT_ONESTEP, CONSTRAINT_NOSEARCH };

struct trellis_constraint_t {
    /* the constraint type */
    constraint_type m_type;

    // expand the previous union into struct to catch some errors.
    /* for CONSTRAINT_ONESTEP type:
       the token of the word. */
    phrase_token_t m_token;
    /* for CONSTRAINT_ONESTEP type:
       the index of the next word.
       for CONSTRAINT_NOSEARCH type:
       the index of the previous onestep constraint. */
    guint32 m_constraint_step;

    trellis_constraint_t(){
        m_type = NO_CONSTRAINT;
    }
};

typedef phrase_token_t lookup_key_t;
/* Key: lookup_key_t, Value: int m, index to m_steps_content[i][m] */
typedef GHashTable * LookupStepIndex;
/* Array of trellis_node */
typedef GArray * LookupStepContent;

/* use maximum heap to get the topest results. */
template<gint32 nbest>
bool get_top_results(size_t num,
                     /* out */ GPtrArray * topresults,
                     /* in */ GPtrArray * candidates) {
    g_ptr_array_set_size(topresults, 0);

    if (0 == candidates->len)
        return false;

    trellis_value_t ** begin =
        (trellis_value_t **) &g_ptr_array_index(candidates, 0);
    trellis_value_t ** end =
        (trellis_value_t **) &g_ptr_array_index(candidates, candidates->len);

    std_lite::make_heap(begin, end, trellis_value_less_than<nbest>);

    while (end != begin) {
        trellis_value_t * one = *begin;
        g_ptr_array_add(topresults, one);

        std_lite::pop_heap(begin, end, trellis_value_less_than<nbest>);
        --end;

        if (topresults->len >= num)
            break;
    }

    /* dump_all_values(topresults); */

    return true;
}

template <gint32 nbest>
class ForwardPhoneticTrellis {
private:
    /* Array of LookupStepIndex */
    GPtrArray * m_steps_index;
    /* Array of LookupStepContent */
    GPtrArray * m_steps_content;

public:
    ForwardPhoneticTrellis() {
        m_steps_index = g_ptr_array_new();
        m_steps_content = g_ptr_array_new();
    }

    ~ForwardPhoneticTrellis() {
        clear();
        g_ptr_array_free(m_steps_index, TRUE);
        g_ptr_array_free(m_steps_content, TRUE);
    }

public:
    size_t size() const {
        assert(m_steps_index->len == m_steps_content->len);
        return m_steps_index->len;
    }

    bool clear() {
        /* clear m_steps_index */
        for ( size_t i = 0; i < m_steps_index->len; ++i){
            LookupStepIndex step_index = (LookupStepIndex) g_ptr_array_index(m_steps_index, i);
            g_hash_table_destroy(step_index);
            g_ptr_array_index(m_steps_index, i) = NULL;
        }

        /* clear m_steps_content */
        for ( size_t i = 0; i < m_steps_content->len; ++i){
            LookupStepContent step_content = (LookupStepContent) g_ptr_array_index(m_steps_content, i);
            g_array_free(step_content, TRUE);
            g_ptr_array_index(m_steps_content, i) = NULL;
        }

        return true;
    }

    bool prepare(gint32 nstep) {
        /* add null start step */
        g_ptr_array_set_size(m_steps_index, nstep);
        g_ptr_array_set_size(m_steps_content, nstep);

        for (int i = 0; i < nstep; ++i) {
            /* initialize m_steps_index */
            g_ptr_array_index(m_steps_index, i) = g_hash_table_new(g_direct_hash, g_direct_equal);
            /* initialize m_steps_content */
            g_ptr_array_index(m_steps_content, i) = g_array_new(FALSE, FALSE, sizeof(trellis_node<nbest>));
        }

        return true;
    }

    /* Array of phrase_token_t */
    bool fill_prefixes(/* in */ TokenVector prefixes) {
        assert(prefixes->len > 0);

        for (size_t i = 0; i < prefixes->len; ++i) {
            phrase_token_t token = g_array_index(prefixes, phrase_token_t, i);
            lookup_key_t initial_key = token;
            trellis_value_t initial_value(log(1.f));
            initial_value.m_handles[1] = token;

            trellis_node<nbest> initial_node;
            assert(initial_node.eval_item(&initial_value));

            LookupStepContent initial_step_content = (LookupStepContent)
                g_ptr_array_index(m_steps_content, 0);
            initial_step_content = g_array_append_val
                (initial_step_content, initial_node);

            LookupStepIndex initial_step_index = (LookupStepIndex)
                g_ptr_array_index(m_steps_index, 0);
            g_hash_table_insert(initial_step_index,
                                GUINT_TO_POINTER(initial_key),
                                GUINT_TO_POINTER(initial_step_content->len - 1));
        }

        return true;
    }

    /* Array of trellis_value_t */
    bool get_candidates(/* in */ gint32 index,
                        /* out */ GPtrArray * candidates) const {
        LookupStepContent step = (LookupStepContent)
            g_ptr_array_index(m_steps_content, index);

        g_ptr_array_set_size(candidates, 0);

        if (0 == step->len)
            return false;

        for (size_t i = 0; i < step->len; ++i) {
            trellis_node<nbest> * node = &g_array_index
                (step, trellis_node<nbest>, i);

            // only initialized in the get_candidates method.
            node->number();

            const trellis_value_t * value = node->begin();
            for (size_t j = 0; j < node->length(); ++j) {
                g_ptr_array_add(candidates, value);
            }
        }

        /* dump_max_value(candidates); */

        return true;
    }

    /* insert candidate */
    bool insert_candidate(gint32 index, lookup_key_t token,
                          const trellis_value_t * candidate) {
        LookupStepIndex step_index = (LookupStepIndex) g_ptr_array_index(m_steps_index, index);
        LookupStepContent step_content = (LookupStepContent) g_ptr_array_index(m_steps_content, index);

        gpointer key = NULL, value = NULL;
        gboolean lookup_result = g_hash_table_lookup_extended
            (step_index, GUINT_TO_POINTER(token), &key, &value);

        if (!lookup_result) {
            trellis_node<nbest> node;
            assert(node.eval_item(candidate));

            g_array_append_val(step_content, node);
            g_hash_table_insert(step_index, GUINT_TO_POINTER(token), GUINT_TO_POINTER(step_content->len - 1));
            return true;
        } else {
            size_t node_index = GPOINTER_TO_UINT(value);
            trellis_node<nbest> * node = &g_array_index
                (step_content, trellis_node<nbest>, node_index);

            return node->eval_item(candidate);
        }

        assert(FALSE);
    }

    /* get tails */
    /* Array of trellis_value_t * */
    bool get_tails(/* out */ GPtrArray * tails) const {
        gint32 tail_index = size() - 1;

        GPtrArray * candidates = g_ptr_array_new();
        get_candidates(tail_index, candidates);
        get_top_results<nbest>(nbest, tails, candidates);

        g_ptr_array_free(candidates, TRUE);
        return true;
    }

    /* get candidate */
    bool get_candidate(gint32 index, lookup_key_t token, gint32 sub_index,
                       const trellis_value_t * & candidate) const {
        LookupStepIndex step_index = (LookupStepIndex) g_ptr_array_index(m_steps_index, index);
        LookupStepContent step_content = (LookupStepContent) g_ptr_array_index(m_steps_content, index);

        gpointer key = NULL, value = NULL;
        gboolean lookup_result = g_hash_table_lookup_extended
            (step_index, GUINT_TO_POINTER(token), &key, &value);

        if (!lookup_result)
            return false;

        size_t node_index = GPOINTER_TO_UINT(value);
        trellis_node<nbest> * node = &g_array_index
            (step_content, trellis_node<nbest>, node_index);

        if (sub_index >= node->length())
            return false;

        candidate = node->begin() + sub_index;

        return true;
    }
};

template <gint32 nbest>
bool extract_result(const ForwardPhoneticTrellis<nbest> * trellis,
                    const trellis_value_t * tail,
                    /* out */ MatchResults & results) {
    /* reset results */
    g_array_set_size(results, trellis->size());
    for (size_t i = 0; i < results->len; ++i){
        phrase_token_t * token = &g_array_index(results, phrase_token_t, i);
        *token = null_token;
    }

    /* backtracing */
    while( true ){
        int index = tail->m_last_step;
        if ( -1 == index )
            break;

        phrase_token_t * token = &g_array_index
            (results, phrase_token_t, index);
        *token = tail->m_handles[1];

        phrase_token_t last_token = tail->m_handles[0];
        int sub_index = tail->m_sub_index;
        assert(trellis->get_candidate(index, last_token, sub_index, tail));
    }

    /* no need to reverse the result */
    return true;
}

#if 0
template <gint32 nbest>
class BackwardPhoneticMatrix {
private:
    /* Array of matrix_step */
    GArray * m_steps_matrix;

public:
    /* set tail node */
    bool set_tail(const matrix_step<nbest> * tail);
    /* back trace */
    /* always assume/assert matrix_step.eval_item(...) return true? */
    bool back_trace(const ForwardPhoneticTrellis * trellis);
    /* extract results */
    int extract(/* out */ GPtrArray * arrays);
};
#endif

class ForwardPhoneticConstraints {
private:
    /* Array of trellis_constraint_t */
    GArray * m_constraints;

protected:
    FacadePhraseIndex * m_phrase_index;

public:
    int add_constraint(size_t start, size_t end, phrase_token_t token);
    bool clear_constraint(size_t index);
    bool validate_constraint(PhoneticKeyMatrix * matrix);

    bool get_constraint(size_t index,
                        const trellis_constraint_t * & constraint) const {
        if (index >= m_constraints->len)
            return false;

        constraint = &g_array_index(m_constraints, trellis_constraint_t, index);

        return true;
    }
};


/* Array of MatchResults */
typedef GPtrArray * NBestMatchResults;

template <gint32 nbest>
class PhoneticLookup {
private:
    const gfloat bigram_lambda;
    const gfloat unigram_lambda;

protected:
    ForwardPhoneticTrellis<nbest> m_trellis;

protected:
    /* saved varibles */
    ForwardPhoneticConstraints m_constraints;
    PhoneticKeyMatrix * m_matrix;

    FacadeChewingTable2 * m_pinyin_table;
    FacadePhraseIndex * m_phrase_index;
    Bigram * m_system_bigram;
    Bigram * m_user_bigram;

protected:
    bool search_unigram2(GPtrArray * topresults,
                         int start, int end,
                         PhraseIndexRanges ranges);
    bool search_bigram2(GPtrArray * topresults,
                        int start, int end,
                        PhraseIndexRanges ranges);

    bool unigram_gen_next_step(int start, int end,
                               trellis_value_t * cur_step,
                               phrase_token_t token);
    bool bigram_gen_next_step(int start, int end,
                              trellis_value_t * cur_step,
                              phrase_token_t token,
                              gfloat bigram_poss);

    bool save_next_step(int next_step_pos, trellis_value_t * cur_step, trellis_value_t * next_step);

public:

    bool get_best_match(TokenVector prefixes,
                        PhoneticKeyMatrix * matrix,
                        ForwardPhoneticConstraints constraints,
                        NBestMatchResults & results);

};

};

#endif