//*****************************************************************************
// Copyright 2017-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#pragma once

#include <cstdio>
#include <iostream>

#include "ngraph/assertion.hpp"
#include "ngraph/distributed.hpp"
#include "ngraph/log.hpp"

#ifdef NGRAPH_DISTRIBUTED_OMPI_ENABLE
#include <string>

#include <mpi.h>

namespace ngraph
{
    namespace distributed
    {
        class OpenMPIDistributedInterface : public DistributedInterface
        {
        public:
            OpenMPIDistributedInterface(
                    const std::string& name = "OpenMPI", 
                    const bool manage_communicator = true)
                : m_name(name), m_manage_communicator(manage_communicator)
            {
                // NGRAPH_ASSERT(m_manage_communicator == false) << "init list works before code body";
                NGRAPH_DEBUG << "mpi ctor";
                if (manage_communicator == true)
                {
                    int is_mpi_initialized = 0;
                    MPI_Initialized(&is_mpi_initialized);
                    NGRAPH_ASSERT(is_mpi_initialized == false) << "Expect to initial MPI comunicator, but MPI had been initialized.";
                    NGRAPH_DEBUG << "MPI_Initialzied returns " << is_mpi_initialized;
                    create_context();
                    m_initialized_mpi = true;
                }
                else
                {
                    int is_mpi_initialized = 0;
                    MPI_Initialized(&is_mpi_initialized);
                    NGRAPH_ASSERT(is_mpi_initialized == false) << "Expect to reuse existing MPI comunicator, but MPI has not been initialized.";
                    MPI_Comm dup_comm;

                }
            }

            ~OpenMPIDistributedInterface() override
            {
                if (m_manage_communicator == true)
                {
                    int is_mpi_finalized = 0;
                    MPI_Finalized(&is_mpi_finalized);
                    if (!is_mpi_finalized && m_initialized_mpi)
                    {
                        free_context();
                        m_initialized_mpi = false;
                    }
                }
            }

            void create_context()
            {
                NGRAPH_DEBUG << "create_context";
                MPI_Init(NULL, NULL);
            }

            void free_context()
            {
                NGRAPH_DEBUG << "free_context";
                MPI_Finalize();
            }

            const std::string& get_name() const override { return m_name; }
            int get_size() override
            {
                int size;
                MPI_Comm_size(MPI_COMM_WORLD, &size);
                return size;
            }

            int get_rank() override
            {
                int rank;
                MPI_Comm_rank(MPI_COMM_WORLD, &rank);
                return rank;
            }

            void log_print(const std::string& timestamp, const std::vector<char>& buf) override
            {
                std::printf(
                    "%s [OpenMPI RANK: %d]: %s\n", timestamp.c_str(), get_rank(), buf.data());
            }

            void all_reduce(void* in,
                            void* out,
                            element::Type_t element_type,
                            reduction::Type reduce_type,
                            size_t count) override
            {
                auto data_type = MPI_FLOAT;

                if (element_type == element::Type_t::f32)
                {
                    data_type = MPI_FLOAT;
                }
                else if (element_type == element::Type_t::f64)
                {
                    data_type = MPI_DOUBLE;
                }
                else
                {
                    throw std::runtime_error("AllReduce op supports only f32 and f64 types");
                }

                decltype(MPI_SUM) mpi_reduce_type;
#if !(defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 8))
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
#endif
                switch (reduce_type)
                {
                case reduction::Type::SUM: mpi_reduce_type = MPI_SUM; break;
                case reduction::Type::PROD: mpi_reduce_type = MPI_PROD; break;
                case reduction::Type::MIN: mpi_reduce_type = MPI_MIN; break;
                case reduction::Type::MAX: mpi_reduce_type = MPI_MAX; break;
                }
#if !(defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ == 8)
#pragma GCC diagnostic pop
#endif

                MPI_Allreduce(in, out, count, data_type, mpi_reduce_type, MPI_COMM_WORLD);
            }

            void broadcast(void* in,
                           element::Type_t element_type,
                           size_t count,
                           int root_id) override
            {
                auto data_type = MPI_FLOAT;

                if (element_type == element::Type_t::f64)
                {
                    data_type = MPI_DOUBLE;
                }
                else if (element_type != element::Type_t::f32)
                {
                    throw std::runtime_error(
                        "BroadcastDistributed op supports only f32 and f64 types");
                }
                MPI_Bcast(in, count, data_type, root_id, MPI_COMM_WORLD);
            }

            void recv(void* in, element::Type_t element_type, size_t count, int src_id) override
            {
                auto data_type = MPI_FLOAT;
                // for send/recv bf16 and f16 can be treat as MPI_SHORT since all are 16bits
                if (element_type == element::Type_t::bf16 || element_type == element::Type_t::f16)
                {
                    data_type = MPI_SHORT;
                }
                else
                {
                    data_type = ngraph_type_to_mpi_type(element_type);
                }

                MPI_Recv(in, count, data_type, src_id, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

            void send(const void* in,
                      element::Type_t element_type,
                      size_t count,
                      int dest_id) override
            {
                auto data_type = MPI_FLOAT;
                // for send/recv bf16 and f16 can be treat as MPI_SHORT since all are 16bits
                if (element_type == element::Type_t::bf16 || element_type == element::Type_t::f16)
                {
                    data_type = MPI_SHORT;
                }
                else
                {
                    data_type = ngraph_type_to_mpi_type(element_type);
                }

                MPI_Send(in, count, data_type, dest_id, 0, MPI_COMM_WORLD);
            }

        protected:
            MPI_Datatype ngraph_type_to_mpi_type(element::Type_t& n_type)
            {
                MPI_Datatype m_type = MPI_FLOAT;
#if !(defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 8))
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
#endif
                switch (n_type)
                {
                case element::Type_t::boolean: m_type = MPI_BYTE; break;
                case element::Type_t::f32: m_type = MPI_FLOAT; break;
                case element::Type_t::f64: m_type = MPI_DOUBLE; break;
                case element::Type_t::i8: m_type = MPI_BYTE; break;
                case element::Type_t::i16: m_type = MPI_SHORT; break;
                case element::Type_t::i32: m_type = MPI_INT; break;
                case element::Type_t::i64: m_type = MPI_LONG; break;
                case element::Type_t::u8: m_type = MPI_UNSIGNED_CHAR; break;
                case element::Type_t::u16: m_type = MPI_UNSIGNED_SHORT; break;
                case element::Type_t::u32: m_type = MPI_UNSIGNED; break;
                case element::Type_t::u64: m_type = MPI_UNSIGNED_LONG; break;
                case element::Type_t::bf16:
                case element::Type_t::f16:
                case element::Type_t::undefined:
                case element::Type_t::dynamic: throw std::runtime_error("unsupported type");
                }
#if !(defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 8))
#pragma GCC diagnostic pop
#endif
                return m_type;
            }

            std::string m_name;
            bool m_initialized_mpi = false;
            bool m_manage_communicator = false;
            int m_rank = 0;
            int m_size = 0;
        };
    }
}
#endif
