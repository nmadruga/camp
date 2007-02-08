! -*- mode: f90; -*-
! Copyright (C) 2005-2007 Nicole Riemer and Matthew West
! Licensed under the GNU General Public License version 2 or (at your
! option) any later version. See the file COPYING for details.
!
! Constant coagulation kernel.

module mod_kernel_constant
contains
  
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  
  subroutine kernel_constant(v1, v2, env, k)

    use mod_environ
    
    real*8, intent(in) :: v1  ! volume of first particle
    real*8, intent(in) :: v2  ! volume of second particle
    real*8, intent(out) :: k ! coagulation kernel
    
    real*8, parameter :: beta_0 = 0.25d0 / (60d0 * 2d8)
    
    type(environ), intent(in) :: env  ! environment state

    k = beta_0
    
  end subroutine kernel_constant
  
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  
  subroutine soln_constant_exp_cond(n_bin, bin_v, bin_g_den, bin_n_den, &
       time, N_0, V_0, rho_p, env)

    use mod_environ
    use mod_util
    use mod_constants
    
    integer, intent(in) :: n_bin        !  number of bins
    real*8, intent(in) :: bin_v(n_bin)  !  volume of particles in bins
    real*8, intent(out) :: bin_g_den(n_bin) ! volume density in bins
    real*8, intent(out) :: bin_n_den(n_bin) ! number density in bins
    
    real*8, intent(in) :: time          !  current time
    real*8, intent(in) :: N_0           !  particle number concentration (#/m^3)
    real*8, intent(in) :: V_0           ! FIXME: what is this?
    real*8, intent(in) :: rho_p         !  particle density (kg/m^3)

    type(environ), intent(in) :: env    ! environment state
    
    real*8 beta_0, tau, T, rat_v, nn, b, x, sigma
    integer k
    
    real*8, parameter :: lambda = 1d0   ! FIXME: what is this?
    
    call kernel_constant(1d0, 1d0, env, beta_0)
    
    if (time .eq. 0d0) then
       do k = 1,n_bin
          bin_n_den(k) = const%pi/2d0 * (2d0*vol2rad(bin_v(k)))**3 * N_0/V_0 &
               * exp(-(bin_v(k)/V_0))
       end do
    else
       tau = N_0 * beta_0 * time
       do k = 1,n_bin
          rat_v = bin_v(k) / V_0
          x = 2d0 * rat_v / (tau + 2d0)
          nn = 4d0 * N_0 / (V_0 * ( tau + 2d0 ) ** 2d0) &
               * exp(-2d0*rat_v/(tau+2d0)*exp(-lambda*tau)-lambda*tau)
          bin_n_den(k) = const%pi/2d0 * (2d0*vol2rad(bin_v(k)))**3d0 * nn
       end do
    end if
    
    do k = 1,n_bin
       bin_g_den(k) = const%pi/6d0 * (2d0*vol2rad(bin_v(k)))**3d0 &
            * bin_n_den(k)
    end do
    
  end subroutine soln_constant_exp_cond
  
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  
end module mod_kernel_constant
