! Copyright (C) 2005-2010 Nicole Riemer and Matthew West
! Licensed under the GNU General Public License version 2 or (at your
! option) any later version. See the file COPYING for details.

!> \file
!> The pmc_coagulation_mpi_controlled module.

!> Aerosol particle coagulation with MPI where each node has its own
!> aero_state, but the root node actually does all coagulation events.
module pmc_coagulation_mpi_controlled

  use pmc_bin_grid
  use pmc_aero_data
  use pmc_util
  use pmc_env_state
  use pmc_aero_state
  use pmc_coagulation
  use pmc_mpi
#ifdef PMC_USE_MPI
  use mpi
#endif

  integer, parameter :: COAG_MAX_BUFFER_SIZE      = 1000000 ! 1 MB
  integer, parameter :: COAG_TAG_REQUEST_PARTICLE = 8851
  integer, parameter :: COAG_TAG_REMOVE_PARTICLE  = 8852
  integer, parameter :: COAG_TAG_SEND_PARTICLE    = 8853
  integer, parameter :: COAG_TAG_DONE             = 8854
  
contains

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  !> Do coagulation for time del_t.
  subroutine mc_coag_mpi_controlled(kernel_type, bin_grid, env_state, &
       aero_data, aero_weight, aero_state, del_t, k_max, tot_n_samp, &
       tot_n_coag)

    !> Coagulation kernel type.
    integer, intent(in) :: kernel_type
    !> Bin grid.
    type(bin_grid_t), intent(in) :: bin_grid
    !> Environment state.
    type(env_state_t), intent(in) :: env_state
    !> Aerosol data.
    type(aero_data_t), intent(in) :: aero_data
    !> Aerosol weight.
    type(aero_weight_t), intent(in) :: aero_weight
    !> Aerosol state.
    type(aero_state_t), intent(inout) :: aero_state
    !> Timestep.
    real(kind=dp), intent(in) :: del_t
    !> Maximum kernel.
    real(kind=dp), intent(in) :: k_max(bin_grid%n_bin,bin_grid%n_bin)
    !> Total number of samples tested.
    integer, intent(out) :: tot_n_samp
    !> Number of coagulation events.
    integer, intent(out) :: tot_n_coag

    logical :: did_coag
    integer :: i, j, n_samp, i_samp, rank, i_proc, n_proc, i_bin
    real(kind=dp) :: accept_factor
    integer, allocatable :: n_parts(:,:)
    real(kind=dp), allocatable :: comp_vols(:)
    
    rank = pmc_mpi_rank()
    n_proc = pmc_mpi_size()
    allocate(n_parts(bin_grid%n_bin, n_proc))
    allocate(comp_vols(n_proc))
    do i_proc = 0,(n_proc - 1)
       call pmc_mpi_transfer_real(aero_state%comp_vol, &
            comp_vols(i_proc + 1), i_proc, 0)
       do i_bin = 1,bin_grid%n_bin
          call pmc_mpi_transfer_integer(aero_state%bin(i_bin)%n_part, &
               n_parts(i_bin, i_proc + 1), i_proc, 0)
       end do
    end do

    tot_n_samp = 0
    tot_n_coag = 0
    if (rank == 0) then
       ! root node actually does the coagulation
       do i = 1,bin_grid%n_bin
          do j = 1,bin_grid%n_bin
             call compute_n_samp(sum(n_parts(i,:)), &
                  sum(n_parts(j,:)), i == j, k_max(i,j), &
                  sum(comp_vols), del_t, n_samp, accept_factor)
             tot_n_samp = tot_n_samp + n_samp
             do i_samp = 1,n_samp
                ! check we still have enough particles to coagulate
                if ((sum(n_parts(i,:)) < 1) &
                     .or. (sum(n_parts(j,:)) < 1) &
                     .or. ((i == j) .and. (sum(n_parts(i,:)) < 2))) then
                   exit
                end if
                call maybe_coag_pair_mpi_controlled(bin_grid, env_state, &
                     aero_data, aero_weight, aero_state, i, j, &
                     kernel_type, accept_factor, did_coag, n_parts, comp_vols)
                if (did_coag) tot_n_coag = tot_n_coag + 1
             enddo
          enddo
       enddo
       ! terminate remote helper node loops
       do i_proc = 0,(n_proc - 1)
          call coag_remote_done(i_proc)
       end do
    else
       ! remote nodes just implement protocol
       call coag_remote_agent(bin_grid, aero_data, aero_state)
    end if
    deallocate(n_parts)
    deallocate(comp_vols)

  end subroutine mc_coag_mpi_controlled

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  !> Implement remote protocol.
  subroutine coag_remote_agent(bin_grid, aero_data, aero_state)

    !> Bin grid.
    type(bin_grid_t), intent(in) :: bin_grid
    !> Aerosol data.
    type(aero_data_t), intent(in) :: aero_data
    !> Aerosol state.
    type(aero_state_t), intent(inout) :: aero_state

#ifdef PMC_USE_MPI
    logical :: done, record_removal
    integer :: status(MPI_STATUS_SIZE), ierr, i_bin, i_part
    integer :: buffer_size, position
    character :: buffer(COAG_MAX_BUFFER_SIZE)
    type(aero_info_t) :: aero_info
    type(aero_particle_t) :: aero_particle

    done = .false.
    do while (.not. done)
       call mpi_recv(buffer, COAG_MAX_BUFFER_SIZE, MPI_CHARACTER, 0, &
            MPI_ANY_TAG, MPI_COMM_WORLD, status, ierr)
       call pmc_mpi_check_ierr(ierr)
       call mpi_get_count(status, MPI_CHARACTER, buffer_size, ierr)
       call pmc_mpi_check_ierr(ierr)
       call assert(132587470, buffer_size < COAG_MAX_BUFFER_SIZE)
       if (status(MPI_TAG) == COAG_TAG_REQUEST_PARTICLE) then
          position = 0
          call pmc_mpi_unpack_integer(buffer, position, i_bin)
          call pmc_mpi_unpack_integer(buffer, position, i_part)
          call assert(743978983, position == buffer_size)

          call assert(256332719, i_bin >= 1)
          call assert(887849380, i_bin <= bin_grid%n_bin)
          call assert(998347627, i_part >= 1)
          call assert(774017621, i_part <= aero_state%bin(i_bin)%n_part)

          position = 0
          call pmc_mpi_pack_aero_particle(buffer, position, &
               aero_state%bin(i_bin)%particle(i_part))
          buffer_size = position
          call assert(797402177, buffer_size < COAG_MAX_BUFFER_SIZE)
          call mpi_send(buffer, buffer_size, MPI_CHARACTER, 0, &
               COAG_TAG_SEND_PARTICLE, MPI_COMM_WORLD, ierr)
          call pmc_mpi_check_ierr(ierr)
       elseif (status(MPI_TAG) == COAG_TAG_REMOVE_PARTICLE) then
          call aero_info_allocate(aero_info)
          position = 0
          call pmc_mpi_unpack_integer(buffer, position, i_bin)
          call pmc_mpi_unpack_integer(buffer, position, i_part)
          call pmc_mpi_unpack_logical(buffer, position, record_removal)
          call pmc_mpi_unpack_aero_info(buffer, position, aero_info)
          call assert(822092586, position == buffer_size)
          
          call assert(703875248, i_bin >= 1)
          call assert(254613726, i_bin <= bin_grid%n_bin)
          call assert(652695715, i_part >= 1)
          call assert(987564730, i_part <= aero_state%bin(i_bin)%n_part)

          call aero_state_remove_particle(aero_state, i_bin, i_part, &
               record_removal, aero_info)
          call aero_info_deallocate(aero_info)
       elseif (status(MPI_TAG) == COAG_TAG_SEND_PARTICLE) then
          call aero_particle_allocate(aero_particle)
          position = 0
          call pmc_mpi_unpack_aero_particle(buffer, position, &
               aero_particle)
          call assert(517349299, position == buffer_size)

          i_bin = aero_particle_in_bin(aero_particle, bin_grid)
          call aero_state_add_particle(aero_state, i_bin, aero_particle)
          call aero_particle_deallocate(aero_particle)
       elseif (status(MPI_TAG) == COAG_TAG_DONE) then
          done = .true.
       else
          call die_msg(734832087, "unknown tag")
       end if
    end do
#endif

  end subroutine coag_remote_agent

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine coag_remote_fetch_particle(aero_state, i_proc, i_bin, i_part, &
       aero_particle)

    !> Aerosol state on node 0.
    type(aero_state_t), intent(in) :: aero_state
    !> Processor to fetch from.
    integer, intent(in) :: i_proc
    !> Bin to fetch from.
    integer, intent(in) :: i_bin
    !> Particle number to fetch.
    integer, intent(in) :: i_part
    !> Particle to fetch into.
    type(aero_particle_t), intent(inout) :: aero_particle
    
#ifdef PMC_USE_MPI
    integer :: buffer_size, position, ierr, status(MPI_STATUS_SIZE)
    character :: buffer(COAG_MAX_BUFFER_SIZE)

    if (i_proc == 0) then
       ! just read it out of aero_state locally on node 0
       call assert(977735278, i_bin >= 1)
       call assert(974530513, i_bin <= size(aero_state%bin))
       call assert(156583013, i_part >= 1)
       call assert(578709336, i_part <= aero_state%bin(i_bin)%n_part)
       call aero_particle_copy(aero_state%bin(i_bin)%particle(i_part), &
            aero_particle)
    else
       ! request particle
       position = 0
       call pmc_mpi_pack_integer(buffer, position, i_bin)
       call pmc_mpi_pack_integer(buffer, position, i_part)
       buffer_size = position
       call assert(771308284, buffer_size < COAG_MAX_BUFFER_SIZE)
       call mpi_send(buffer, buffer_size, MPI_CHARACTER, i_proc, &
            COAG_TAG_REQUEST_PARTICLE, MPI_COMM_WORLD, ierr)
       call pmc_mpi_check_ierr(ierr)

       ! get particle
       call mpi_recv(buffer, COAG_MAX_BUFFER_SIZE, MPI_CHARACTER, i_proc, &
            COAG_TAG_SEND_PARTICLE, MPI_COMM_WORLD, status, ierr)
       call pmc_mpi_check_ierr(ierr)
       call mpi_get_count(status, MPI_CHARACTER, buffer_size, ierr)
       call pmc_mpi_check_ierr(ierr)
       call assert(752384662, buffer_size < COAG_MAX_BUFFER_SIZE)

       position = 0
       call pmc_mpi_unpack_aero_particle(buffer, position, aero_particle)
       call assert(739976989, position == buffer_size)
    end if
#endif

  end subroutine coag_remote_fetch_particle

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine coag_remote_remove_particle(aero_state, i_proc, i_bin, i_part, &
       record_removal, aero_info)

    !> Aerosol state on node 0.
    type(aero_state_t), intent(inout) :: aero_state
    !> Processor to remove from.
    integer, intent(in) :: i_proc
    !> Bin to remove from.
    integer, intent(in) :: i_bin
    !> Particle number to remove.
    integer, intent(in) :: i_part
    !> Whether to record the removal in the aero_info_array.
    logical, intent(in) :: record_removal
    !> Removal info.
    type(aero_info_t), intent(in) :: aero_info
    
#ifdef PMC_USE_MPI
    integer :: buffer_size, position, ierr
    character :: buffer(COAG_MAX_BUFFER_SIZE)

    if (i_proc == 0) then
       ! just do it directly on the local aero_state
       call aero_state_remove_particle(aero_state, i_bin, i_part, &
            record_removal, aero_info)
    else
       position = 0
       call pmc_mpi_pack_integer(buffer, position, i_bin)
       call pmc_mpi_pack_integer(buffer, position, i_part)
       call pmc_mpi_pack_logical(buffer, position, record_removal)
       call pmc_mpi_pack_aero_info(buffer, position, aero_info)
       buffer_size = position
       
       call assert(521039594, buffer_size < COAG_MAX_BUFFER_SIZE)
       call mpi_send(buffer, buffer_size, MPI_CHARACTER, i_proc, &
            COAG_TAG_REMOVE_PARTICLE, MPI_COMM_WORLD, ierr)
       call pmc_mpi_check_ierr(ierr)
    end if
#endif

  end subroutine coag_remote_remove_particle

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine coag_remote_add_particle(bin_grid, aero_state, i_proc, &
       aero_particle)

    !> Bin grid.
    type(bin_grid_t), intent(in) :: bin_grid
    !> Aerosol state on node 0.
    type(aero_state_t), intent(inout) :: aero_state
    !> Processor to add to.
    integer, intent(in) :: i_proc
    !> Particle to add.
    type(aero_particle_t), intent(in) :: aero_particle
    
#ifdef PMC_USE_MPI
    integer :: buffer_size, position, i_bin, ierr
    character :: buffer(COAG_MAX_BUFFER_SIZE)

    if (i_proc == 0) then
       ! just do it directly on the local aero_state
       i_bin = aero_particle_in_bin(aero_particle, bin_grid)
       call aero_state_add_particle(aero_state, i_bin, aero_particle)
    else
       position = 0
       call pmc_mpi_pack_aero_particle(buffer, position, aero_particle)
       buffer_size = position
       
       call assert(159937224, buffer_size < COAG_MAX_BUFFER_SIZE)
       call mpi_send(buffer, buffer_size, MPI_CHARACTER, i_proc, &
            COAG_TAG_SEND_PARTICLE, MPI_COMM_WORLD, ierr)
       call pmc_mpi_check_ierr(ierr)
    end if
#endif

  end subroutine coag_remote_add_particle

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  subroutine coag_remote_done(i_proc)

    !> Processor to send "done" to.
    integer, intent(in) :: i_proc
    
#ifdef PMC_USE_MPI
    integer :: buffer_size, ierr
    character :: buffer(COAG_MAX_BUFFER_SIZE)

    if (i_proc > 0) then
       buffer_size = 0
       call assert(503494624, buffer_size < COAG_MAX_BUFFER_SIZE)
       call mpi_send(buffer, buffer_size, MPI_CHARACTER, i_proc, &
            COAG_TAG_DONE, MPI_COMM_WORLD, ierr)
       call pmc_mpi_check_ierr(ierr)
    end if
#endif

  end subroutine coag_remote_done

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  !> Choose a random pair for potential coagulation and test its
  !> probability of coagulation. If it happens, do the coagulation and
  !> update all structures.
  !!
  !! The probability of a coagulation will be taken as <tt>(kernel /
  !! k_max)</tt>.
  subroutine maybe_coag_pair_mpi_controlled(bin_grid, env_state, &
       aero_data, aero_weight, aero_state, b1, b2, kernel_type, &
       accept_factor, did_coag, n_parts, comp_vols)

    !> Bin grid.
    type(bin_grid_t), intent(in) :: bin_grid
    !> Environment state.
    type(env_state_t), intent(in) :: env_state
    !> Aerosol data.
    type(aero_data_t), intent(in) :: aero_data
    !> Aerosol weight.
    type(aero_weight_t), intent(in) :: aero_weight
    !> Aerosol state.
    type(aero_state_t), intent(inout) :: aero_state
    !> Bin of first particle.
    integer, intent(in) :: b1
    !> Bin of second particle.
    integer, intent(in) :: b2
    !> Coagulation kernel type.
    integer, intent(in) :: kernel_type
    !> Scale factor for accept probability (1).
    real(kind=dp), intent(in) :: accept_factor
    !> Whether a coagulation occured.
    logical, intent(out) :: did_coag
    !> Number of particles per-bin and per-processor.
    integer :: n_parts(:, :)
    !> Computational volumes for each processor.
    real(kind=dp) :: comp_vols(:)
    
    integer :: p1, p2, s1, s2
    real(kind=dp) :: p, k
    type(aero_particle_t) :: particle_1, particle_2
    
    call aero_particle_allocate(particle_1)
    call aero_particle_allocate(particle_2)

    call assert(717627403, sum(n_parts(b1,:)) >= 1)
    call assert(994541886, sum(n_parts(b2,:)) >= 1)
    if (b1 == b2) then
       call assert(968872192, sum(n_parts(b1,:)) >= 2)
    end if
    
    did_coag = .false.
    
    call find_rand_pair_mpi_controlled(n_parts, b1, b2, p1, p2, s1, s2)
    call coag_remote_fetch_particle(aero_state, p1, b1, s1, particle_1)
    call coag_remote_fetch_particle(aero_state, p2, b2, s2, particle_2)
    call weighted_kernel(kernel_type, particle_1, &
         particle_2, aero_data, aero_weight, env_state, k)
    p = k * accept_factor

    if (pmc_random() .lt. p) then
       call coagulate_mpi_controlled(bin_grid, aero_data, aero_weight, &
            aero_state, p1, b1, s1, p2, b2, s2, particle_1, particle_2, &
            n_parts, comp_vols)
       did_coag = .true.
    end if

    call aero_particle_deallocate(particle_1)
    call aero_particle_deallocate(particle_2)
    
  end subroutine maybe_coag_pair_mpi_controlled
  
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  !> Given bins b1 and b2, find a random pair of particles (b1, s1)
  !> and (b2, s2) that are not the same particle particle as each
  !> other.
  subroutine find_rand_pair_mpi_controlled(n_parts, b1, b2, p1, p2, s1, s2)
    
    !> Number of particles per-bin and per-processor.
    integer :: n_parts(:,:)
    !> Bin number of first particle.
    integer, intent(in) :: b1
    !> Bin number of second particle.
    integer, intent(in) :: b2
    !> Processor of first particle.
    integer, intent(out) :: p1
    !> Processor of second particle.
    integer, intent(out) :: p2
    !> First rand particle.
    integer, intent(out) :: s1
    !> Second rand particle.
    integer, intent(out) :: s2

    ! check we have enough particles to avoid being stuck in an
    ! infinite loop below
    call assert(329209888, sum(n_parts(b1,:)) >= 1)
    call assert(745799706, sum(n_parts(b2,:)) >= 1)
    if (b1 == b2) then
       call assert(755331221, sum(n_parts(b1,:)) >= 2)
    end if
    
    ! FIXME: rand() only returns a REAL*4, so we might not be able to
    ! generate all integers between 1 and M if M is too big.

100 s1 = int(pmc_random() * dble(sum(n_parts(b1,:)))) + 1
    if ((s1 .lt. 1) .or. (s1 .gt. sum(n_parts(b1,:)))) goto 100
101 s2 = int(pmc_random() * dble(sum(n_parts(b2,:)))) + 1
    if ((s2 .lt. 1) .or. (s2 .gt. sum(n_parts(b2,:)))) goto 101
    if ((b1 .eq. b2) .and. (s1 .eq. s2)) goto 101

    do p1 = 0,(pmc_mpi_size() - 1)
       if (s1 <= n_parts(b1, p1 + 1)) then
          exit
       end if
       s1 = s1 - n_parts(b1, p1 + 1)
    end do
    do p2 = 0,(pmc_mpi_size() + 1)
       if (s2 <= n_parts(b2, p2 + 1)) then
          exit
       end if
       s2 = s2 - n_parts(b2, p2 + 1)
    end do
    
  end subroutine find_rand_pair_mpi_controlled
  
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  !> Join together particles (b1, s1) and (b2, s2), updating all
  !> particle and bin structures to reflect the change.
  subroutine coagulate_mpi_controlled(bin_grid, aero_data, aero_weight, &
       aero_state, p1, b1, s1, p2, b2, s2, aero_particle_1, &
       aero_particle_2, n_parts, comp_vols)

    !> Bin grid.
    type(bin_grid_t), intent(in) :: bin_grid
    !> Aerosol data.
    type(aero_data_t), intent(in) :: aero_data
    !> Aerosol weight.
    type(aero_weight_t), intent(in) :: aero_weight
    !> Aerosol state.
    type(aero_state_t), intent(inout) :: aero_state
    !> Processor of first particle.
    integer, intent(in) :: p1
    !> First particle (bin number).
    integer, intent(in) :: b1
    !> First particle (number in bin).
    integer, intent(in) :: s1
    !> Processor of second particle.
    integer, intent(in) :: p2
    !> Second particle (bin number).
    integer, intent(in) :: b2
    !> Second particle (number in bin).
    integer, intent(in) :: s2
    !> Copy of particle_1
    type(aero_particle_t) :: aero_particle_1
    !> Copy of particle_2
    type(aero_particle_t) :: aero_particle_2
    !> Number of particles per-bin and per-processor.
    integer :: n_parts(:, :)
    !> Computational volumes for each processor.
    real(kind=dp) :: comp_vols(:)
    
    type(aero_particle_t) :: aero_particle_new
    type(aero_info_t) :: aero_info_1, aero_info_2
    logical :: remove_1, remove_2, create_new, id_1_lost, id_2_lost
    integer :: pn, bn

    call aero_particle_allocate(aero_particle_new)
    call aero_info_allocate(aero_info_1)
    call aero_info_allocate(aero_info_2)

    call coagulate_weighting(aero_particle_1, aero_particle_2, &
         aero_particle_new, aero_data, aero_weight, remove_1, remove_2, &
         create_new, id_1_lost, id_2_lost, aero_info_1, aero_info_2)

    ! remove particles
    if ((p1 == p2) .and. (b1 == b2) .and. (s2 > s1)) then
       ! handle a tricky corner case where we have to watch for s2 or
       ! s1 being the last entry in the array and being repacked when
       ! the other one is removed
       if (remove_2) then
          call coag_remote_remove_particle(aero_state, p2, b2, s2, &
               id_2_lost, aero_info_2)
       end if
       if (remove_1) then
          call coag_remote_remove_particle(aero_state, p1, b1, s1, &
               id_1_lost, aero_info_1)
       end if
    else
       if (remove_1) then
          call coag_remote_remove_particle(aero_state, p1, b1, s1, &
               id_1_lost, aero_info_1)
       end if
       if (remove_2) then
          call coag_remote_remove_particle(aero_state, p2, b2, s2, &
               id_2_lost, aero_info_2)
       end if
    end if

    ! add new particle
    if (create_new) then
       bn = aero_particle_in_bin(aero_particle_new, bin_grid)
       pn = sample_cts_pdf(size(comp_vols), comp_vols) - 1
       call coag_remote_add_particle(bin_grid, aero_state, pn, &
            aero_particle_new)
    end if

    ! fix up n_parts
    n_parts(b1, p1 + 1) = n_parts(b1, p1 + 1) - 1
    n_parts(b2, p2 + 1) = n_parts(b2, p2 + 1) - 1
    n_parts(bn, pn + 1) = n_parts(bn, pn + 1) + 1
    
    call aero_particle_deallocate(aero_particle_new)
    call aero_info_deallocate(aero_info_1)
    call aero_info_deallocate(aero_info_2)

  end subroutine coagulate_mpi_controlled

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  
end module pmc_coagulation_mpi_controlled
