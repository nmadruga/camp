module camp_debug_2

  use mpi
  use camp_constants,                  only : i_kind, dp
  implicit none

  integer :: rank,ierr,n_ranks

contains

  subroutine init_export_f_state()
    call mpi_comm_rank(MPI_COMM_WORLD, rank, ierr)
    call mpi_comm_size(MPI_COMM_WORLD, n_ranks, ierr)
    if(rank==0) then
      open(50, file="out/state.csv", status="replace", action="write")
      close(50)
    end if
  end subroutine

  subroutine export_f_state(x, len, len2)
    real(kind=dp), dimension(:), intent(in) :: x
    integer, intent(in) :: len, len2
    integer :: k,j,i
    call mpi_comm_rank(MPI_COMM_WORLD, rank, ierr)
    call mpi_comm_size(MPI_COMM_WORLD, n_ranks, ierr)
    do k=0,len2-1!camp_core%n_cells
      do j=0, n_ranks-1
        if(rank==j) then
          open(50, file="out/state.csv", status="old", position="append", action="write")
          do i=1, len!camp_core%size_state_per_cell
            write(50, "(ES23.15)") x(i+k*len) !camp_state%state_var(i)
          end do
          close(50)
        end if
        call mpi_barrier(MPI_COMM_WORLD, ierr)
      end do
    end do
  end subroutine

end module