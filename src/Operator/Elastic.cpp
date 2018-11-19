#include "Model/Solid/Elastic/Isotropic.H"
#include "Model/Solid/Elastic/Cubic.H"
#include "Model/Solid/Elastic/Degradable/Isotropic.H"
#include "Elastic.H"


namespace Operator
{
template<class T>
Elastic<T>::Elastic (const Vector<Geometry>& a_geom,
		     const Vector<BoxArray>& a_grids,
		     const Vector<DistributionMapping>& a_dmap,
		     const LPInfo& a_info)
{
	BL_PROFILE("Operator::Elastic::Elastic()");
	Util::Message(INFO);

	define(a_geom, a_grids, a_dmap, a_info);
}

template<class T>
Elastic<T>::~Elastic ()
{}

template<class T>
void
Elastic<T>::define (const Vector<Geometry>& a_geom,
		    const Vector<BoxArray>& a_grids,
		    const Vector<DistributionMapping>& a_dmap,
		    const LPInfo& a_info,
		    const Vector<FabFactory<FArrayBox> const*>& a_factory)
{
	BL_PROFILE("Operator::Elastic::define()");
	//Util::Message(INFO);

	Operator::define(a_geom,a_grids,a_dmap,a_info,a_factory);

	int model_nghost = 1;

	model.resize(m_num_amr_levels);
	for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
	{
		model[amrlev].resize(m_num_mg_levels[amrlev]);
		for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
		{
			model[amrlev][mglev].reset(new MultiTab(amrex::convert(m_grids[amrlev][mglev],
									       amrex::IntVect::TheNodeVector()),
								m_dmap[amrlev][mglev], 1, model_nghost));
		}
	}
}

template <class T>
void
Elastic<T>::SetModel (int amrlev, const amrex::FabArray<amrex::BaseFab<T> >& a_model)
{
	BL_PROFILE("Operator::Elastic::SetModel()");
	//Util::Message(INFO);

	for (MFIter mfi(a_model, true); mfi.isValid(); ++mfi)
	{
		Util::Message(INFO);
		const Box& bx = mfi.tilebox();
		amrex::BaseFab<T> &modelfab = (*(model[amrlev][0]))[mfi];
		const amrex::BaseFab<T> &a_modelfab = a_model[mfi];
		Util::Message(INFO);

		AMREX_D_TERM(for (int m1 = bx.loVect()[0]-1; m1<=bx.hiVect()[0]+1; m1++),
			     for (int m2 = bx.loVect()[1]-1; m2<=bx.hiVect()[1]+1; m2++),
			     for (int m3 = bx.loVect()[2]-1; m3<=bx.hiVect()[2]+1; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			//Util::Message(INFO,"box = ",bx);
			//Util::Message(INFO,"Point = ",m);
			//Util::Message(INFO,"Modelfab = ",modelfab(m));
			//Util::Message(INFO,"InputModel fab = ",a_modelfab (m));
			modelfab(m) = a_modelfab(m);
		}
	}
}

template<class T>
void
Elastic<T>::Fapply (int amrlev, int mglev, MultiFab& f, const MultiFab& u) const
{
	BL_PROFILE(Color::FG::Yellow + "Operator::Elastic::Fapply()" + Color::Reset);
	//Util::Message(INFO);

	amrex::Box domain(m_geom[amrlev][mglev].Domain());
	const Real* DX = m_geom[amrlev][mglev].CellSize();
	
	// // DEBUG: Check to see if Fapply is getting passed bad values.
	// for (MFIter mfi(f, true); mfi.isValid(); ++mfi)
	// {
	// 	const amrex::FArrayBox &ufab    = u[mfi];
	// 	if(ufab.contains_inf()) Util::Abort(INFO, "Inf in ufab [before update]");
	// 	if(ufab.contains_nan()) Util::Abort(INFO, "Nan in ufab [before update]");
	// }

	for (MFIter mfi(f, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		amrex::BaseFab<T> &C = (*(model[amrlev][mglev]))[mfi];
		const amrex::FArrayBox &ufab    = u[mfi];
		amrex::FArrayBox       &ffab    = f[mfi];

		AMREX_D_TERM(for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]; m1++),
			     for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++),
			     for (int m3 = bx.loVect()[2]; m3<=bx.hiVect()[2]; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			bool    AMREX_D_DECL(xmin = (m1 == domain.loVect()[0]),
					     ymin = (m2 == domain.loVect()[1]),
					     zmin = (m3 == domain.loVect()[2])),
				AMREX_D_DECL(xmax = (m1 == domain.hiVect()[0] + 1),
					     ymax = (m2 == domain.hiVect()[1] + 1),
					     zmax = (m3 == domain.hiVect()[2] + 1));

			// The displacement gradient tensor
			Set::Matrix gradu; // gradu(i,j) = u_{i,j)

			// The gradient of the displacement gradient tensor
			std::array<Set::Matrix,AMREX_SPACEDIM> gradgradu; // gradgradu[k](l,j) = u_{k,lj}

			// Fill gradu and gradgradu
			for (int i = 0; i < AMREX_SPACEDIM; i++)
			{
				if (std::isnan(ufab(m,i))) Util::Abort(INFO,"u is nan. m = ",m,", i = ", i, ", amrlev=", amrlev, " mglev=", mglev);
				if (std::isinf(ufab(m,i))) Util::Abort(INFO,"u is inf. m = ",m,", i = ", i, ", amrlev=", amrlev, " mglev=", mglev);
				// Note: the (?:) block modifies the stencil if the node is on a boundary

				AMREX_D_TERM(gradu(i,0) = ((!xmax ? ufab(m+dx[0],i) : ufab(m,i)) - (!xmin ? ufab(m-dx[0],i) : ufab(m,i)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);,
					     gradu(i,1) = ((!ymax ? ufab(m+dx[1],i) : ufab(m,i)) - (!ymin ? ufab(m-dx[1],i) : ufab(m,i)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);,
					     gradu(i,2) = ((!zmax ? ufab(m+dx[2],i) : ufab(m,i)) - (!zmin ? ufab(m-dx[2],i) : ufab(m,i)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]););
			
				AMREX_D_TERM(gradgradu[i](0,0) = (ufab(m+dx[0],i) - 2.0*ufab(m,i) + ufab(m-dx[0],i))/DX[0]/DX[0];
					     ,// 2D
					     gradgradu[i](0,1) = (ufab(m+dx[0]+dx[1],i) + ufab(m-dx[0]-dx[1],i) - ufab(m+dx[0]-dx[1],i) - ufab(m-dx[0]+dx[1],i))/(2.0*DX[0])/(2.0*DX[1]);
					     gradgradu[i](1,0) = gradgradu[i](0,1);
					     gradgradu[i](1,1) = (ufab(m+dx[1],i) - 2.0*ufab(m,i) + ufab(m-dx[1],i))/DX[1]/DX[1];
					     ,// 3D
					     gradgradu[i](0,2) = (ufab(m+dx[0]+dx[2],i) + ufab(m-dx[0]-dx[2],i) - ufab(m+dx[0]-dx[2],i) - ufab(m-dx[0]+dx[2],i))/(2.0*DX[0])/(2.0*DX[2]);
					     gradgradu[i](1,2) = (ufab(m+dx[1]+dx[2],i) + ufab(m-dx[1]-dx[2],i) - ufab(m+dx[1]-dx[2],i) - ufab(m-dx[1]+dx[2],i))/(2.0*DX[1])/(2.0*DX[2]);
					     gradgradu[i](2,0) = gradgradu[i](0,2);
					     gradgradu[i](2,1) = gradgradu[i](1,2);
					     gradgradu[i](2,2) = (ufab(m+dx[2],i) - 2.0*ufab(m,i) + ufab(m-dx[2],i))/DX[2]/DX[2];);
			}

			// Strain tensor
			Set::Matrix eps = 0.5*(gradu + gradu.transpose());
			// Stress tensor computed using the model fab
			Set::Matrix sig = C(m)(eps);

			//
			// Boundary conditions
			//
			// BCs are implemented as boundary operators.
			//
			// ┌                      ┐ ┌                     ┐   ┌              ┐
			// │              |       │ │ interior		  │   │ body	     │
			// │  Div C Grad  │       │ │ displacements	  │   │ forces	     │
			// │              │       │ │			  │ = │		     │
			// │ ─────────────┼────── │ │ ──────────────────  │   │ ──────────── │
			// │              │ Bndry │ │ bndry displacements │   │ bndry values │
			// └                      ┘ └			  ┘   └		     ┘
			//
			// For displacement:
			//   (Bndry)(u) = u
			// For traction:
			//   (Bndry)(u) = C Grad (u) n   (n is surface normal)
			//
			// The displacement values or traction values are set as the boundary
			// values of the rhs fab.
			//
			for (int i = 0; i < AMREX_SPACEDIM; i++) // iterate over DIMENSIONS
			{
				for (int j = 0; j < AMREX_SPACEDIM; j++) // iterate over FACES
				{
					if (m[j] == domain.loVect()[j])
					{
						if (m_bc_lo[j][i] == BC::Displacement)
							ffab(m,i) = ufab(m,i);
						else if (m_bc_lo[j][i] == BC::Traction) 
							ffab(m,i) = -sig(i,j);
						else Util::Abort(INFO, "Invalid BC");
					}
					if (m[j] == domain.hiVect()[j] + 1)
					{
						if (m_bc_hi[j][i] == BC::Displacement)
							ffab(m,i) = ufab(m,i);
						else if (m_bc_hi[j][i] == BC::Traction) 
							ffab(m,i) = +sig(i,j);
						else Util::Abort(INFO, "Invalid BC");
					}
				}
			}
			if (AMREX_D_TERM(xmax || xmin, || ymax || ymin, || zmax || zmin)) continue;

			// Util::Message(INFO,"u lovect = ",amrex::IntVect(ufab.loVect()), ", C lovect = ",amrex::IntVect(C.loVect()));
			// Util::Message(INFO,"u lovect = ",amrex::IntVect(ufab.hiVect()), ", C lovect = ",amrex::IntVect(C.hiVect()));
			// Util::Message(INFO,"u xmax");ufab(m+dx[0]);
			// Util::Message(INFO,"C xmax");C(m+dx[0]);
			// Util::Message(INFO,"u ymax");ufab(m+dx[1]);
			// Util::Message(INFO,"C ymax");C(m+dx[1]);

			//
			// Operator
			//
			// The return value is
			//    f = C(grad grad u) + grad(C)*grad(u)
			// In index notation
			//    f_i = C_{ijkl,j} u_{k,l}  +  C_{ijkl}u_{k,lj}
			//
			Set::Vector f =
				C(m)(gradgradu) + 
				AMREX_D_TERM(((C(m+dx[0]) - C(m-dx[0]))/2.0/DX[0])(gradu).col(0),
				      	     + ((C(m+dx[1]) - C(m-dx[1]))/2.0/DX[1])(gradu).col(1),
				       	     + ((C(m+dx[2]) - C(m-dx[2]))/2.0/DX[2])(gradu).col(2));

			for (int i = 0; i < AMREX_SPACEDIM; i++)
				ffab(m,i) = f(i);
		}
	}
}



template<class T>
void
Elastic<T>::Diagonal (int amrlev, int mglev, MultiFab& diag)
{
	BL_PROFILE("Operator::Elastic::Diagonal()");
	//Util::Message(INFO);

	amrex::Box domain(m_geom[amrlev][mglev].Domain());
	const Real* DX = m_geom[amrlev][mglev].CellSize();
	
	for (MFIter mfi(diag, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		amrex::BaseFab<T> &C = (*(model[amrlev][mglev]))[mfi];
		amrex::FArrayBox       &diagfab    = diag[mfi];

		AMREX_D_TERM(for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]; m1++),
		 	     for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++),
		 	     for (int m3 = bx.loVect()[2]; m3<=bx.hiVect()[2]; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			bool    AMREX_D_DECL(xmin = (m1 == domain.loVect()[0]),
					     ymin = (m2 == domain.loVect()[1]),
					     zmin = (m3 == domain.loVect()[2])),
				AMREX_D_DECL(xmax = (m1 == domain.hiVect()[0] + 1),
					     ymax = (m2 == domain.hiVect()[1] + 1),
					     zmax = (m3 == domain.hiVect()[2] + 1));

			Set::Matrix gradu; // gradu(i,j) = u_{i,j)
			std::array<Set::Matrix,AMREX_SPACEDIM> gradgradu; // gradgradu[k](l,j) = u_{k,lj}

			for (int i = 0; i < AMREX_SPACEDIM; i++)
			{
				diagfab(m,i) = 0.0;
				for (int k = 0; k < AMREX_SPACEDIM; k++)
				{
					AMREX_D_TERM(gradu(k,0) = ((!xmax ? 0.0 : (i==k ? 1.0 : 0.0)) - (!xmin ? 0.0 : (i==k ? 1.0 : 0.0)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);,
						     gradu(k,1) = ((!ymax ? 0.0 : (i==k ? 1.0 : 0.0)) - (!ymin ? 0.0 : (i==k ? 1.0 : 0.0)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);,
						     gradu(k,2) = ((!zmax ? 0.0 : (i==k ? 1.0 : 0.0)) - (!zmin ? 0.0 : (i==k ? 1.0 : 0.0)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]););
			
					AMREX_D_TERM(gradgradu[k](0,0) = (i==k ? -2.0 : 0.0)/DX[0]/DX[0];
						     ,// 2D
						     gradgradu[k](0,1) = 0.0;
						     gradgradu[k](1,0) = 0.0;
						     gradgradu[k](1,1) = (i==k ? -2.0 : 0.0)/DX[1]/DX[1];
						     ,// 3D
						     gradgradu[k](0,2) = 0.0;
						     gradgradu[k](1,2) = 0.0;
						     gradgradu[k](2,0) = 0.0;
						     gradgradu[k](2,1) = 0.0;
						     gradgradu[k](2,2) = (i==k ? -2.0 : 0.0)/DX[2]/DX[2]);
				}

				Set::Matrix eps = 0.5*(gradu + gradu.transpose());
				Set::Matrix sig = C(m)(eps);

				if (AMREX_D_TERM(xmax || xmin, || ymax || ymin, || zmax || zmin)) 
				{
					for (int j = 0; j < AMREX_SPACEDIM; j++) // iterate over FACES
					{
						if (m[j] == domain.loVect()[j])
						{
							if (m_bc_lo[j][i] == BC::Displacement)
								diagfab(m,i) += 1.0;
							else if (m_bc_lo[j][i] == BC::Traction) 
								diagfab(m,i) -= sig(i,j);
							else Util::Abort(INFO, "Invalid BC");
						}
						if (m[j] == domain.hiVect()[j] + 1)
						{
							if (m_bc_hi[j][i] == BC::Displacement)
								diagfab(m,i) += 1.0;
							else if (m_bc_hi[j][i] == BC::Traction) 
								diagfab(m,i) += sig(i,j);
							else Util::Abort(INFO, "Invalid BC");
						}
					}
				}
				else
				{
					Set::Vector f =
						C(m)(gradgradu) + 
						AMREX_D_TERM(((C(m+dx[0]) - C(m-dx[0]))/2.0/DX[0])(eps).col(0),
						  	     + ((C(m+dx[1]) - C(m-dx[1]))/2.0/DX[1])(eps).col(1),
						    	     + ((C(m+dx[2]) - C(m-dx[2]))/2.0/DX[2])(eps).col(2));
					diagfab(m,i) += f(i);
					//if (diagfab(m,i) != diagfab(m,i))
						// Util::Message(INFO,"diagfab=",diagfab(m,i)," amrlev=",amrlev," mglev=",mglev," m = " , m , " i = ", i, " C = \n", C(m),
						// 	      " C(m+dx[0]) = \n", C(m+dx[0]),
						// 	      " C(m-dx[0]) = \n", C(m-dx[0]));
				}
			}
		}
	}
}




template<class T>
void
Elastic<T>::FFlux (int /*amrlev*/, const MFIter& /*mfi*/,
		const std::array<FArrayBox*,AMREX_SPACEDIM>& sigmafab,
		const FArrayBox& /*ufab*/, const int /*face_only*/) const
{
	BL_PROFILE("Operator::Elastic::FFlux()");
	//Util::Message(INFO);
	amrex::BaseFab<amrex::Real> AMREX_D_DECL( &fxfab = *sigmafab[0],
	 					  &fyfab = *sigmafab[1],
	 					  &fzfab = *sigmafab[2] ) ;
	AMREX_D_TERM(fxfab.setVal(0.0);,
	 	     fyfab.setVal(0.0);,
	 	     fzfab.setVal(0.0););

}

template<class T>
void
Elastic<T>::Strain  (int amrlev,
		    amrex::MultiFab& eps,
		    const amrex::MultiFab& u,
		    bool voigt) const
{
	amrex::Box domain(m_geom[amrlev][0].Domain());
	if (voigt)
		AMREX_ASSERT(eps.nComp() == (AMREX_SPACEDIM*(AMREX_SPACEDIM-1)/2));
	else
		AMREX_ASSERT(eps.nComp() == AMREX_SPACEDIM*AMREX_SPACEDIM);
	
	const amrex::Real* DX = m_geom[amrlev][0].CellSize();
	
	for (MFIter mfi(u, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		amrex::FArrayBox  &epsfab   = eps[mfi];
		const amrex::FArrayBox  &ufab = u[mfi];
		
		AMREX_D_TERM(for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]; m1++),
			     for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++),
			     for (int m3 = bx.loVect()[2]; m3<=bx.hiVect()[2]; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			bool	AMREX_D_DECL(xmin = (m1 == bx.loVect()[0]),
					     ymin = (m2 == bx.loVect()[1]),
					     zmin = (m3 == bx.loVect()[2])),
				AMREX_D_DECL(xmax = (m1 == bx.hiVect()[0]),
					     ymax = (m2 == bx.hiVect()[1]),
					     zmax = (m3 == bx.hiVect()[2]));

			Set::Matrix gradu;
			AMREX_D_TERM(gradu(0,0) = ((!xmax ? ufab(m+dx[0],0) : ufab(m,0)) - (!xmin ? ufab(m-dx[0],0) : ufab(m,0)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     ,
				     gradu(0,1) = ((!ymax ? ufab(m+dx[1],0) : ufab(m,0)) - (!ymin ? ufab(m-dx[1],0) : ufab(m,0)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(1,0) = ((!xmax ? ufab(m+dx[0],1) : ufab(m,1)) - (!xmin ? ufab(m-dx[0],1) : ufab(m,1)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(1,1) = ((!ymax ? ufab(m+dx[1],1) : ufab(m,1)) - (!ymin ? ufab(m-dx[1],1) : ufab(m,1)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     ,
				     gradu(0,2) = ((!zmax ? ufab(m+dx[2],0) : ufab(m,0)) - (!zmin ? ufab(m-dx[2],0) : ufab(m,0)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(1,2) = ((!zmax ? ufab(m+dx[2],1) : ufab(m,1)) - (!zmin ? ufab(m-dx[2],1) : ufab(m,1)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(2,0) = ((!xmax ? ufab(m+dx[0],2) : ufab(m,2)) - (!xmin ? ufab(m-dx[0],2) : ufab(m,2)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(2,1) = ((!ymax ? ufab(m+dx[1],2) : ufab(m,2)) - (!ymin ? ufab(m-dx[1],2) : ufab(m,2)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(2,2) = ((!zmax ? ufab(m+dx[2],2) : ufab(m,2)) - (!zmin ? ufab(m-dx[2],2) : ufab(m,2)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]););
			
			Set::Matrix strain = 0.5 * (gradu + gradu.transpose());
			
			if (voigt)
			{
#if AMREX_SPACEDIM == 2
				epsfab(m,0) = strain(0,0); epsfab(m,1) = strain(1,1); epsfab(m,2) = strain(0,1); 
#elif AMREX_SPACEDIM == 3
				epsfab(m,0) = strain(0,0); epsfab(m,1) = strain(1,1); epsfab(m,2) = strain(2,2); 
				epsfab(m,3) = strain(1,2); epsfab(m,4) = strain(2,0); epsfab(m,5) = strain(0,1); 
#endif
			}
			else
			{
#if   AMREX_SPACEDIM == 2
				epsfab(m,0) = strain(0,0); epsfab(m,1) = strain(0,1); 
				epsfab(m,2) = strain(1,0); epsfab(m,3) = strain(1,1); 
#elif AMREX_SPACEDIM == 3
				epsfab(m,0) = strain(0,0); epsfab(m,1) = strain(0,1); epsfab(m,2) = strain(0,2); 
				epsfab(m,3) = strain(1,0); epsfab(m,4) = strain(1,1); epsfab(m,5) = strain(1,2); 
				epsfab(m,6) = strain(2,0); epsfab(m,7) = strain(2,1); epsfab(m,8) = strain(2,2); 
#endif
			}
		}
	}
}


template<class T>
void
Elastic<T>::Stress (int amrlev,
		    amrex::MultiFab& sigma,
		    const amrex::MultiFab& u,
		    bool voigt) const
{
	BL_PROFILE("Operator::Elastic::Stress()");
	//Util::Message(INFO);
	amrex::Box domain(m_geom[amrlev][0].Domain());
	if (voigt)
		AMREX_ASSERT(sigma.nComp() == (AMREX_SPACEDIM*(AMREX_SPACEDIM-1)/2));
	else
		AMREX_ASSERT(sigma.nComp() == AMREX_SPACEDIM*AMREX_SPACEDIM);

	const amrex::Real* DX = m_geom[amrlev][0].CellSize();

	Util::Message(INFO,"u grow = ", u.nGrow());

	for (MFIter mfi(u, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		amrex::BaseFab<T> &C          = (*(model[amrlev][0]))[mfi];
		amrex::FArrayBox  &sigmafab   = sigma[mfi];
		const amrex::FArrayBox  &ufab = u[mfi];

		AMREX_D_TERM(for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]; m1++),
			     for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++),
			     for (int m3 = bx.loVect()[2]; m3<=bx.hiVect()[2]; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			//Util::Message(INFO,"m=",m," & box = (",bx.loVect()[0],",",bx.loVect()[1],",",bx.loVect()[2],")(",bx.hiVect()[0],",",bx.hiVect()[1],",",bx.hiVect()[2],")");

			bool    AMREX_D_DECL(xmin = (m1 == bx.loVect()[0]),
					     ymin = (m2 == bx.loVect()[1]),
					     zmin = (m3 == bx.loVect()[2])),
				AMREX_D_DECL(xmax = (m1 == bx.hiVect()[0]),
					     ymax = (m2 == bx.hiVect()[1]),
					     zmax = (m3 == bx.hiVect()[2]));

			Set::Matrix gradu;

			AMREX_D_TERM(gradu(0,0) = ((!xmax ? ufab(m+dx[0],0) : ufab(m,0)) - (!xmin ? ufab(m-dx[0],0) : ufab(m,0)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     ,
				     gradu(0,1) = ((!ymax ? ufab(m+dx[1],0) : ufab(m,0)) - (!ymin ? ufab(m-dx[1],0) : ufab(m,0)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(1,0) = ((!xmax ? ufab(m+dx[0],1) : ufab(m,1)) - (!xmin ? ufab(m-dx[0],1) : ufab(m,1)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(1,1) = ((!ymax ? ufab(m+dx[1],1) : ufab(m,1)) - (!ymin ? ufab(m-dx[1],1) : ufab(m,1)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     ,
				     gradu(0,2) = ((!zmax ? ufab(m+dx[2],0) : ufab(m,0)) - (!zmin ? ufab(m-dx[2],0) : ufab(m,0)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(1,2) = ((!zmax ? ufab(m+dx[2],1) : ufab(m,1)) - (!zmin ? ufab(m-dx[2],1) : ufab(m,1)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(2,0) = ((!xmax ? ufab(m+dx[0],2) : ufab(m,2)) - (!xmin ? ufab(m-dx[0],2) : ufab(m,2)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(2,1) = ((!ymax ? ufab(m+dx[1],2) : ufab(m,2)) - (!ymin ? ufab(m-dx[1],2) : ufab(m,2)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(2,2) = ((!zmax ? ufab(m+dx[2],2) : ufab(m,2)) - (!zmin ? ufab(m-dx[2],2) : ufab(m,2)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]););
			
			Set::Matrix eps = 0.5 * (gradu + gradu.transpose());

			Set::Matrix sig = C(m)(eps);

			if (voigt)
			{
#if AMREX_SPACEDIM == 2
				sigmafab(m,0) = sig(0,0); sigmafab(m,1) = sig(1,1); sigmafab(m,2) = sig(0,1); 
#elif AMREX_SPACEDIM == 3
				sigmafab(m,0) = sig(0,0); sigmafab(m,1) = sig(1,1); sigmafab(m,2) = sig(2,2); 
				sigmafab(m,3) = sig(1,2); sigmafab(m,4) = sig(2,0); sigmafab(m,5) = sig(0,1); 
#endif
			}
			else
			{
#if   AMREX_SPACEDIM == 2
				sigmafab(m,0) = sig(0,0); sigmafab(m,1) = sig(0,1); 
				sigmafab(m,2) = sig(1,0); sigmafab(m,3) = sig(1,1); 
#elif AMREX_SPACEDIM == 3
				sigmafab(m,0) = sig(0,0); sigmafab(m,1) = sig(0,1); sigmafab(m,2) = sig(0,2); 
				sigmafab(m,3) = sig(1,0); sigmafab(m,4) = sig(1,1); sigmafab(m,5) = sig(1,2); 
				sigmafab(m,6) = sig(2,0); sigmafab(m,7) = sig(2,1); sigmafab(m,8) = sig(2,2); 
#endif
			}
			}
	}
}


template<class T>
void
Elastic<T>::Energy (int amrlev,
		    amrex::MultiFab& energy,
		    const amrex::MultiFab& u) const
{
	BL_PROFILE("Operator::Elastic::Energy()");
	amrex::Box domain(m_geom[amrlev][0].Domain());
	AMREX_ASSERT(energy.nComp() == 1);
	AMREX_ASSERT(u.nComp() == AMREX_SPACEDIM);

	const amrex::Real* DX = m_geom[amrlev][0].CellSize();

	for (MFIter mfi(u, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		amrex::BaseFab<T> &C          = (*(model[amrlev][0]))[mfi];
		amrex::FArrayBox  &energyfab  = energy[mfi];
		const amrex::FArrayBox  &ufab = u[mfi];

		AMREX_D_TERM(for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]; m1++),
			     for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++),
			     for (int m3 = bx.loVect()[2]; m3<=bx.hiVect()[2]; m3++))
		{
			amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
			bool    AMREX_D_DECL(xmin = (m1 == bx.loVect()[0]),
					     ymin = (m2 == bx.loVect()[1]),
					     zmin = (m3 == bx.loVect()[2])),
				AMREX_D_DECL(xmax = (m1 == bx.hiVect()[0]),
					     ymax = (m2 == bx.hiVect()[1]),
					     zmax = (m3 == bx.hiVect()[2]));


			Set::Matrix gradu;

			AMREX_D_TERM(gradu(0,0) = ((!xmax ? ufab(m+dx[0],0) : ufab(m,0)) - (!xmin ? ufab(m-dx[0],0) : ufab(m,0)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     ,
				     gradu(0,1) = ((!ymax ? ufab(m+dx[1],0) : ufab(m,0)) - (!ymin ? ufab(m-dx[1],0) : ufab(m,0)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(1,0) = ((!xmax ? ufab(m+dx[0],1) : ufab(m,1)) - (!xmin ? ufab(m-dx[0],1) : ufab(m,1)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(1,1) = ((!ymax ? ufab(m+dx[1],1) : ufab(m,1)) - (!ymin ? ufab(m-dx[1],1) : ufab(m,1)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     ,
				     gradu(0,2) = ((!zmax ? ufab(m+dx[2],0) : ufab(m,0)) - (!zmin ? ufab(m-dx[2],0) : ufab(m,0)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(1,2) = ((!zmax ? ufab(m+dx[2],1) : ufab(m,1)) - (!zmin ? ufab(m-dx[2],1) : ufab(m,1)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]);
				     gradu(2,0) = ((!xmax ? ufab(m+dx[0],2) : ufab(m,2)) - (!xmin ? ufab(m-dx[0],2) : ufab(m,2)))/((xmin || xmax ? 1.0 : 2.0)*DX[0]);
				     gradu(2,1) = ((!ymax ? ufab(m+dx[1],2) : ufab(m,2)) - (!ymin ? ufab(m-dx[1],2) : ufab(m,2)))/((ymin || ymax ? 1.0 : 2.0)*DX[1]);
				     gradu(2,2) = ((!zmax ? ufab(m+dx[2],2) : ufab(m,2)) - (!zmin ? ufab(m-dx[2],2) : ufab(m,2)))/((zmin || zmax ? 1.0 : 2.0)*DX[2]););
			
			Set::Matrix eps = 0.5 * (gradu + gradu.transpose());
			Set::Matrix sig = C(m)(eps);

			energyfab(m) = (eps.transpose() * sig).trace();

		}
	}
}


template<class T>
void
Elastic<T>::reflux (int crse_amrlev,
		    MultiFab& res, const MultiFab& crse_sol, const MultiFab& crse_rhs,
		    MultiFab& fine_res, MultiFab& fine_sol, const MultiFab& fine_rhs) const
{
	
	BL_PROFILE("Operator::Elastic::reflux()");

#if AMREX_SPACEDIM == 2

	int ncomp = AMREX_SPACEDIM;

	const Geometry& cgeom = m_geom[crse_amrlev  ][0];
 	const Geometry& fgeom = m_geom[crse_amrlev+1][0];
 	const Box& c_cc_domain = cgeom.Domain();
 	const Box& c_nd_domain = amrex::surroundingNodes(c_cc_domain);
	const Real* cDX = cgeom.CellSize();
	const Real* fDX = fgeom.CellSize();

 	const BoxArray& fba = fine_sol.boxArray();
 	const DistributionMapping& fdm = fine_sol.DistributionMap();

 	const iMultiFab& fdmsk = *m_dirichlet_mask[crse_amrlev+1][0];

 	MultiFab fine_res_for_coarse(amrex::coarsen(fba, 2), fdm, ncomp, 1);
	fine_res_for_coarse.setVal(0.0);

 	applyBC(crse_amrlev+1, 0, fine_res, BCMode::Inhomogeneous, StateMode::Solution);

	Set::Scalar fDX_cDX = (fDX[0]*fDX[1]) / (cDX[0]*cDX[1]);

	for (MFIter mfi(fine_res_for_coarse, true); mfi.isValid(); ++mfi)
	{
		const Box& bx = mfi.tilebox();
		FArrayBox &fine = fine_res[mfi];
		FArrayBox &crse = fine_res_for_coarse[mfi];
		const FArrayBox &ufab = fine_sol[mfi];
		TArrayBox &C = (*(model[crse_amrlev+1][0]))[mfi];

		for (int m2 = bx.loVect()[1]+1; m2<=bx.hiVect()[1]-1; m2++)
			for (int m1 = bx.loVect()[0]; m1<=bx.hiVect()[0]-1; m1++)
			{
				amrex::IntVect m_crse(AMREX_D_DECL(m1,m2,m3));
				amrex::IntVect m_fine(AMREX_D_DECL(m1*2,m2*2,m3*2));
				
				if (m1 == bx.loVect()[0])
				{

					Set::Matrix gradu;
					gradu(0,0) =
						+ 0.25*(ufab(m_fine+dx[0] +dx[1],0) - ufab(m_fine +dx[1],0))/fDX[0]
						+ 0.50*(ufab(m_fine+dx[0]       ,0) - ufab(m_fine       ,0))/fDX[0]
						+ 0.25*(ufab(m_fine+dx[0] -dx[1],0) - ufab(m_fine -dx[1],0))/fDX[0];
					gradu(1,0) =
						+ 0.25*(ufab(m_fine+dx[0] +dx[1],1) - ufab(m_fine +dx[1],1))/fDX[0]
						+ 0.50*(ufab(m_fine+dx[0]       ,1) - ufab(m_fine       ,1))/fDX[0]
						+ 0.25*(ufab(m_fine+dx[0] -dx[1],1) - ufab(m_fine -dx[1],1))/fDX[0];
					gradu(0,1) =
						+ 0.25*(ufab(m_fine+dx[1] +dx[1],0) - ufab(m_fine-dx[1] +dx[1],0))/(2*fDX[1])
						+ 0.50*(ufab(m_fine+dx[1]       ,0) - ufab(m_fine-dx[1]       ,0))/(2*fDX[1])
						+ 0.25*(ufab(m_fine+dx[1] -dx[1],0) - ufab(m_fine-dx[1] -dx[1],0))/(2*fDX[1]);
					gradu(1,1) =
						+ 0.25*(ufab(m_fine+dx[1] +dx[1],1) - ufab(m_fine-dx[1] +dx[1],1))/(2*fDX[1])
						+ 0.50*(ufab(m_fine+dx[1]       ,1) - ufab(m_fine-dx[1]       ,1))/(2*fDX[1])
						+ 0.25*(ufab(m_fine+dx[1] -dx[1],1) - ufab(m_fine-dx[1] -dx[1],1))/(2*fDX[1]);

					Set::Matrix eps = 0.5*(gradu + gradu.transpose());
					Set::Matrix sig = C(m_fine)(eps);
					Set::Vector n(-1, 0);
					Set::Vector t = sig*n;

					for (int n = 0 ; n < ncomp; n++)
					{
						crse(m_crse,n) -= t(n) / (0.5*fDX[0]);
					}
				}
				else if (m1 == bx.hiVect()[0])
				{
					//crse(m_crse,n) = 0.25*(fine(m_fine-dx[1],n) + 2.0*fine(m_fine,n) + fine(m_fine+dx[1],n));
				}
				else
				{
					for (int n = 0 ; n < ncomp; n++)
					{

						crse(m_crse,n) =
							((+     fine(m_fine-dx[0]-dx[1],n) + 2.0*fine(m_fine-dx[1],n) +     fine(m_fine+dx[0]-dx[1],n)
							  + 2.0*fine(m_fine-dx[0]      ,n) + 4.0*fine(m_fine      ,n) + 2.0*fine(m_fine+dx[0]      ,n) 
							  +     fine(m_fine-dx[0]+dx[1],n) + 2.0*fine(m_fine+dx[1],n) +     fine(m_fine+dx[0]+dx[1],n))/16.0);
					}
				}
		}
	}

	res.ParallelCopy(fine_res_for_coarse,0,0,ncomp,1,1,cgeom.periodicity());
	return;
	if (!m_testing) return;

	//
	// GOOD UNTIL THIS POINT
	//


 	const iMultiFab& nd_mask     = *m_nd_fine_mask[crse_amrlev];
 	const iMultiFab& cc_mask     = *m_cc_fine_mask[crse_amrlev];
 	const auto& has_fine_bndry = m_has_fine_bndry[crse_amrlev];

	static int crse_cell = 0;
	static int fine_cell = 1;
	static int crse_node = 0;
	static int crse_fine_node = 1;
	static int fine_node = 2;

 	for (MFIter mfi(res, MFItInfo().EnableTiling().SetDynamic(true)); mfi.isValid(); ++mfi)
 	{
 		if ((*has_fine_bndry)[mfi])
 		{
			amrex::BaseFab<T> &C = (*(model[crse_amrlev][0]))[mfi];

 			const Box& bx = mfi.tilebox();
			
			const amrex::FArrayBox &ufab = crse_sol[mfi];
			amrex::FArrayBox &resfab = res[mfi];
			
			int m1 = 2;//bx.loVect()[0];
			for (int m2 = bx.loVect()[1]; m2<=bx.hiVect()[1]; m2++)
			{
				amrex::IntVect m_crse(AMREX_D_DECL(m1,m2,m3));
				
				Set::Matrix gradu;
				gradu(0,0) = (ufab(m_crse,0) - ufab(m_crse-dx[0],0))/cDX[0];
				gradu(1,0) = (ufab(m_crse,1) - ufab(m_crse-dx[0],1))/cDX[0];
				gradu(0,1) = (ufab(m_crse+dx[1],0) - ufab(m_crse-dx[1],0))/(2*cDX[1]);
				gradu(1,1) = (ufab(m_crse+dx[1],1) - ufab(m_crse-dx[1],1))/(2*cDX[1]);
				Set::Matrix eps = 0.5*(gradu + gradu.transpose());
				Set::Matrix sig = C(m_crse)(eps);
				
				Set::Vector n(1.0, 0.0);

				Set::Vector t = sig*n;

				resfab(m_crse,0) -= t(0) / (0.5*cDX[0]);
				resfab(m_crse,1) -= t(1) / (0.5*cDX[0]);
			}

			// continue;
			// for (int m1 = bx.loVect()[0]; m1 <= bx.hiVect()[0]; m1++)
			// for (int m2 = bx.loVect()[1]; m2 <= bx.hiVect()[1]; m2++)
			// {
			// 	amrex::IntVect m(m1,m2);
			// 	if (!bx.contains(m)) continue;
			// 	if (nd_mask[mfi](m) != crse_fine_node) continue; // Only proceed if on a coarse/fine boundary

				
			// 	if (nd_mask[mfi](m) == crse_fine_node) 
			// 	 	Util::Message(INFO,"CRSE FINE BNDRY: res(",m1,",",m2,") = ",res[mfi](m,1));
			// 	// else
			// 	// 	Util::Message(INFO,"NOT CRSE FINE BNDRY: res(",m1,",",m2,") = ",res[mfi](m,1));
				
			// }
		}
	}





	//
	// YOU ... SHALL ... NOT ... PASS!
	// 
	return;


#if (0)

	MultiFab fine_contrib(amrex::coarsen(fba, 2), fdm, ncomp, 1);
	fine_contrib.setVal(0.0);


	const auto& fmodel = *model[crse_amrlev+1][0];
	FArrayBox Axfab;

        for (MFIter mfi(fine_contrib, MFItInfo().EnableTiling().SetDynamic(true));
             mfi.isValid(); ++mfi)
	{
		//Util::Abort(INFO);
		const Box& cvbx = mfi.validbox();         const int* cglo = cvbx.loVect(), *cghi = cvbx.hiVect();// cglo, cghi
		const Box& fvbx = amrex::refine(cvbx,2);  const int* glo  = fvbx.loVect(), *ghi  = fvbx.hiVect();// glo, ghi
		const Box& cbx = mfi.tilebox();           const int* clo  = cbx.loVect(),  *chi  = cbx.hiVect();// clo, chi
		const Box& fbx = amrex::refine(cbx,2);    const int* lo   = fbx.loVect(),  *hi   = fbx.hiVect();// lo, hi

		// amrex_mlndlap_res_fine_contrib(BL_TO_FORTRAN_BOX(cbx), // clo, chi
		// 			       BL_TO_FORTRAN_BOX(cvbx), // cglo, cghi
		// 			       BL_TO_FORTRAN_ANYD(fine_contrib[mfi]), // f, flo, fhi
		// 			       BL_TO_FORTRAN_ANYD(fine_sol[mfi]), // x, xlo, xhi
		// 			       BL_TO_FORTRAN_ANYD(sigfab), // sig, slo, shi
		// 			       BL_TO_FORTRAN_ANYD(Axfab), // Ax alo, ahi
		// 			       BL_TO_FORTRAN_ANYD(fdmsk[mfi]), // msk, mlo, mhi
		// 			       fdxinv); // dxinv

		amrex::BaseFab<T> &C = (*(model[crse_amrlev+1][0]))[mfi];

		const Box& bx_Ax = amrex::grow(fbx,1);
		const Box& b2 = bx_Ax & amrex::grow(fvbx,-1);
		Axfab.resize(bx_Ax,ncomp);
		Axfab.setVal(0.0);
		Axfab.copy(fine_rhs[mfi], b2, 0, b2, 0, ncomp);
		Axfab.minus(fine_res[mfi], b2, 0, 0, ncomp);

		amrex::IntVect gtlo(std::max(lo[0]-1, glo[0]),
				    std::max(lo[1]-1, glo[1]));

		amrex::IntVect gthi(std::min(hi[0]+1, ghi[0]),
				    std::min(hi[1]+1, ghi[1]));

		FArrayBox &ufab = fine_sol[mfi];
		FArrayBox &ffab = fine_contrib[mfi];

		for (int m2 = gtlo[1]; m2<=gthi[1]; m2++)
		{	
			int step;
			if (m2 == glo[1] || m2 == ghi[1]) step = 1;
			else step=gthi[0] - gtlo[0];

			for (int m1 = gtlo[0]; m1<=gthi[0]; m1 += step)
			{
				if (m1 == glo[0] || m1 == ghi[0] || step==1)
				{
					amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));

					Set::Matrix gradu; 
					std::array<Set::Matrix,AMREX_SPACEDIM> gradgradu; 

					// Fill gradu and gradgradu
					for (int i = 0; i < AMREX_SPACEDIM; i++)
					{
						AMREX_D_TERM(gradu(i,0) = (ufab(m+dx[0],i) - ufab(m-dx[0],i))/(2.0*fDX[0]);,
							     gradu(i,1) = (ufab(m+dx[1],i) - ufab(m-dx[1],i))/(2.0*fDX[1]);,
							     gradu(i,2) = (ufab(m+dx[2],i) - ufab(m-dx[2],i))/(2.0*fDX[2]););

						AMREX_D_TERM(gradgradu[i](0,0) = (ufab(m+dx[0],i) - 2.0*ufab(m,i) + ufab(m-dx[0],i))/fDX[0]/fDX[0];
							     ,// 2D
							     gradgradu[i](0,1) = (ufab(m+dx[0]+dx[1],i) + ufab(m-dx[0]-dx[1],i) - ufab(m+dx[0]-dx[1],i) - ufab(m-dx[0]+dx[1],i))/(2.0*fDX[0])/(2.0*fDX[1]);
							     gradgradu[i](1,0) = gradgradu[i](0,1);
							     gradgradu[i](1,1) = (ufab(m+dx[1],i) - 2.0*ufab(m,i) + ufab(m-dx[1],i))/fDX[1]/fDX[1];
							     ,// 3D
							     gradgradu[i](0,2) = (ufab(m+dx[0]+dx[2],i) + ufab(m-dx[0]-dx[2],i) - ufab(m+dx[0]-dx[2],i) - ufab(m-dx[0]+dx[2],i))/(2.0*fDX[0])/(2.0*fDX[2]);
							     gradgradu[i](1,2) = (ufab(m+dx[1]+dx[2],i) + ufab(m-dx[1]-dx[2],i) - ufab(m+dx[1]-dx[2],i) - ufab(m-dx[1]+dx[2],i))/(2.0*fDX[1])/(2.0*fDX[2]);
							     gradgradu[i](2,0) = gradgradu[i](0,2);
							     gradgradu[i](2,1) = gradgradu[i](1,2);
							     gradgradu[i](2,2) = (ufab(m+dx[2],i) - 2.0*ufab(m,i) + ufab(m-dx[2],i))/fDX[2]/fDX[2];);
					}

					// Strain tensor
					Set::Matrix eps = 0.5*(gradu + gradu.transpose());
					// Stress tensor computed using the model fab
					Set::Matrix sig = C(m)(eps);

					Set::Vector f =
						C(m)(gradgradu) 
						 + AMREX_D_TERM(((C(m+dx[0]) - C(m-dx[0]))/2.0/fDX[0])(gradu).col(0),
						 	       + ((C(m+dx[1]) - C(m-dx[1]))/2.0/fDX[1])(gradu).col(1),
						 	       + ((C(m+dx[2]) - C(m-dx[2]))/2.0/fDX[2])(gradu).col(2))
						;
					for (int i = 0; i < AMREX_SPACEDIM; i++)
						Axfab(m,i) += f(i);
				}
			}
		}

		for (int m2 = gtlo[1]; m2<=gthi[1]; m2++)
		{	
			int step;
			if (m2 == glo[1] || m2 == ghi[1]) step = 1;
			else step=gthi[0] - gtlo[0];

			for (int m1 = gtlo[0]; m1<=gthi[0]; m1 += step)
			{
				if (m1 == glo[0] || m1 == ghi[0] || step==1)
				{
					amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
					
				}
			}
		}
	}

	MultiFab fine_contrib_on_crse(crse_sol.boxArray(), crse_sol.DistributionMap(), ncomp, 0);
	fine_contrib_on_crse.setVal(0.0);
	fine_contrib_on_crse.ParallelAdd(fine_contrib, cgeom.periodicity());


 	const iMultiFab& cdmsk = *m_dirichlet_mask[crse_amrlev][0];
 	const iMultiFab& nd_mask     = *m_nd_fine_mask[crse_amrlev];
 	const iMultiFab& cc_mask     = *m_cc_fine_mask[crse_amrlev];
 	const auto& has_fine_bndry = m_has_fine_bndry[crse_amrlev];


// 	const auto& csigma = *m_sigma[crse_amrlev][0][0];

	static int crse_cell = 0;
	static int fine_cell = 1;
	static int crse_node = 0;
	static int crse_fine_node = 1;
	static int fine_node = 2;
 	for (MFIter mfi(res, MFItInfo().EnableTiling().SetDynamic(true)); mfi.isValid(); ++mfi)
 	{
 		if ((*has_fine_bndry)[mfi])
 		{
			amrex::BaseFab<T> &C = (*(model[crse_amrlev][0]))[mfi];



 			const Box& bx = mfi.tilebox();
 			// amrex_mlndlap_res_cf_contrib(BL_TO_FORTRAN_BOX(bx),  <<<< lo, hi
 			// 			     BL_TO_FORTRAN_ANYD(res[mfi]), <<<< res, rlo, rhi [OUTPUT]
 			// 			     BL_TO_FORTRAN_ANYD(crse_sol[mfi]), <<< phi, phlo, phhi
 			// 			     BL_TO_FORTRAN_ANYD(crse_rhs[mfi]), <<< rhs, rhlo, rhhi
 			// 			     BL_TO_FORTRAN_ANYD(csigma[mfi]), <<< sig, slo, shi
 			// 			     BL_TO_FORTRAN_ANYD(cdmsk[mfi]), <<< mask, mlo, mhi
 			// 			     BL_TO_FORTRAN_ANYD((*nd_mask)[mfi]), <<< ndmsk, nmlo, nmhi
 			// 			     BL_TO_FORTRAN_ANYD((*cc_mask)[mfi]), <<< ccmsk, cmlo, cmhi
 			// 			     BL_TO_FORTRAN_ANYD(fine_contrib_on_crse[mfi]), <<< fc, clo, chi [OUTPUT]
 			// 			     cdxinv, <<< dxinv
			//                           BL_TO_FORTRAN_BOX(c_nd_domain), <<< ndlo, ndhi
 			// 			     m_lobc.data(),  <<< bclo
			//                           m_hibc.data());  <<< bchi
			
			const amrex::FArrayBox &ufab = crse_sol[mfi];
			
			for (int m1 = bx.loVect()[0]-1; m1 <= bx.hiVect()[0]+1; m1++)
			for (int m2 = bx.loVect()[1]-1; m2 <= bx.hiVect()[1]+1; m2++)
			{
				amrex::IntVect m(m1,m2);


				if (!bx.contains(m)) continue;
				if (nd_mask[mfi](m) != crse_fine_node) continue; // Only proceed if on a coarse/fine boundary
				
				// Util::Message(INFO,"m=(",m1,",",m2,")");
				// continue;

				Set::Vector Ax = Set::Vector::Zero();
				Set::Vector nx(1.0, 0), ny(0,1.0);

				Set::Matrix gradu;
				

				for (int i = 0; i < ncomp ; i++)
				{
					//               if (ccmsk(i-1,j-1) .eq. crse_cell) then
					//                  Ax = Ax + sig(i-1,j-1)*(facx*(2.d0*(phi(i-1,j  )-phi(i  ,j  )) &
					//                       &                       +     (phi(i-1,j-1)-phi(i  ,j-1))) &
					//                       &                + facy*(2.d0*(phi(i  ,j-1)-phi(i  ,j  )) &
					//                       &                       +     (phi(i-1,j-1)-phi(i-1,j  ))))
					//               end if

					if (cc_mask[mfi](m-dx[0]-dx[1]) ==  crse_cell) // xlo or ylo
					{
						
						std::array<Set::Matrix,AMREX_SPACEDIM> gradgradu; // gradgradu[k](l,j) = u_{k,lj}
						
						
						
						//Util::Message(INFO,"m=",m," A");
						gradu(i,0) = (ufab(m,i) - ufab(m-dx[0],i))/(cDX[0]);
						gradu(i,1) = (ufab(m,i) - ufab(m-dx[1],i))/(cDX[1]);
						Set::Matrix eps = 0.5*(gradu + gradu.transpose());
						Set::Matrix sig = C(m)(eps);
						Ax -= sig*nx;
					}
					if (cc_mask[mfi](m-dx[1]) ==  crse_cell) // xhi or ylo
					{
						//Util::Message(INFO,"m=",m," B");
						gradu(i,0) = (ufab(m+dx[0],i) - ufab(m,i))/(cDX[0]);
						gradu(i,1) = (ufab(m,i) - ufab(m-dx[1],i))/(cDX[1]);
						Set::Matrix eps = 0.5*(gradu + gradu.transpose());
						Set::Matrix sig = C(m)(eps);
						Ax -= sig*ny;
					}
					if (cc_mask[mfi](m-dx[0]) ==  crse_cell) // xlo or yhi
					{
						//Util::Message(INFO,"m=",m," C");
						gradu(i,0) = (ufab(m,i) - ufab(m-dx[0],i))/(cDX[0]);
						gradu(i,1) = (ufab(m+dx[1],i) - ufab(m,i))/(cDX[1]);
						Set::Matrix eps = 0.5*(gradu + gradu.transpose());
						Set::Matrix sig = C(m)(eps);
						Ax += sig*nx;
					}
					if (cc_mask[mfi](m) ==  crse_cell) // xhi or yhi
					{
						//Util::Message(INFO,"m=",m," D");
						gradu(i,0) = (ufab(m+dx[0],i) - ufab(m,i))/(cDX[0]);
						gradu(i,1) = (ufab(m+dx[1],i) - ufab(m,i))/(cDX[1]);
						Set::Matrix eps = 0.5*(gradu + gradu.transpose());
						Set::Matrix sig = C(m)(eps);
						Ax += sig*ny;
					}
				}

				Set::Vector Axf;
				Axf(0) = fine_contrib_on_crse[mfi](m,0);
				Axf(1) = fine_contrib_on_crse[mfi](m,1);
				
				res[mfi](m,0) = crse_rhs[mfi](m,0) - (Ax(0) + Axf(0));
				res[mfi](m,1) = crse_rhs[mfi](m,1) - (Ax(1) + Axf(1));

			}

	  
			//   integer :: i,j
			//   real(amrex_real) :: Ax, Axf, facx, facy

			//   facx = (1.d0/6.d0)*dxinv(1)*dxinv(1)
			//   facy = (1.d0/6.d0)*dxinv(2)*dxinv(2)

			//   do    j = lo(2), hi(2)
			//      do i = lo(1), hi(1)
			//         if (dmsk(i,j) .ne. dirichlet) then
			//            if (ndmsk(i,j) .eq. crse_fine_node) then
			//               Ax = 0.d0
			//               if (ccmsk(i-1,j-1) .eq. crse_cell) then
			//                  Ax = Ax + sig(i-1,j-1)*(facx*(2.d0*(phi(i-1,j  )-phi(i  ,j  )) &
			//                       &                       +     (phi(i-1,j-1)-phi(i  ,j-1))) &
			//                       &                + facy*(2.d0*(phi(i  ,j-1)-phi(i  ,j  )) &
			//                       &                       +     (phi(i-1,j-1)-phi(i-1,j  ))))
			//               end if
			//               if (ccmsk(i,j-1) .eq. crse_cell) then
			//                  Ax = Ax + sig(i,j-1)*(facx*(2.d0*(phi(i+1,j  )-phi(i  ,j  )) &
			//                       &                     +     (phi(i+1,j-1)-phi(i  ,j-1))) &
			//                       &              + facy*(2.d0*(phi(i  ,j-1)-phi(i  ,j  )) &
			//                       &                     +     (phi(i+1,j-1)-phi(i+1,j  ))))
			//               end if
			//               if (ccmsk(i-1,j) .eq. crse_cell) then
			//                  Ax = Ax + sig(i-1,j)*(facx*(2.d0*(phi(i-1,j  )-phi(i  ,j  )) &
			//                       &                     +     (phi(i-1,j+1)-phi(i  ,j+1))) &
			//                       &              + facy*(2.d0*(phi(i  ,j+1)-phi(i  ,j  )) &
			//                       &                     +     (phi(i-1,j+1)-phi(i-1,j  ))))
			//               end if
			//               if (ccmsk(i,j) .eq. crse_cell) then
			//                  Ax = Ax + sig(i,j)*(facx*(2.d0*(phi(i+1,j  )-phi(i  ,j  )) &
			//                       &                  +      (phi(i+1,j+1)-phi(i  ,j+1))) &
			//                       &            + facy*(2.d0*(phi(i  ,j+1)-phi(i  ,j  )) &
			//                       &                  +      (phi(i+1,j+1)-phi(i+1,j  ))))
			//               end if

			//               Axf = fc(i,j)

			//               if (i .eq. ndlo(1) .and. &
			//                    (    bclo(1) .eq. amrex_lo_neumann &
			//                    .or. bclo(1) .eq. amrex_lo_inflow)) then
			//                  Axf = 2.d0*Axf
			//               else if (i.eq. ndhi(1) .and. &
			//                    (    bchi(1) .eq. amrex_lo_neumann &
			//                    .or. bchi(1) .eq. amrex_lo_inflow)) then
			//                  Axf = 2.d0*Axf
			//               end if

			//               if (j .eq. ndlo(2) .and. &
			//                    (    bclo(2) .eq. amrex_lo_neumann &
			//                    .or. bclo(2) .eq. amrex_lo_inflow)) then
			//                  Axf = 2.d0*Axf
			//               else if (j .eq. ndhi(2) .and. &
			//                    (    bchi(2) .eq. amrex_lo_neumann &
			//                    .or. bchi(2) .eq. amrex_lo_inflow)) then
			//                  Axf = 2.d0*Axf
			//               end if

			//               res(i,j) = rhs(i,j) - (Ax + Axf)
			//            end if
			//         end if
			//      end do
			//   end do
			// end subroutine amrex_mlndlap_res_cf_contrib
 		}
 	}
#endif

#else
	Util::Abort(INFO, "reflux not implemented in 3D. Turn AMR off or switch to 2D.");
#endif

}

template<class T>
void
Elastic<T>::averageDownCoeffs ()
{
	BL_PROFILE("Elastic::averageDownCoeffs()");
	//Util::Message(INFO);
	
	// for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
	// {
	// 	for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
	// 	{
	// 		///\todo replace number of ghost cells with 0
	// 		///\todo I think we can erase this section.
	// 		model[amrlev][mglev].reset(new amrex::FabArray<amrex::BaseFab<T> >(m_grids[amrlev][mglev], m_dmap[amrlev][mglev], 1, 1));
	// 	}
	// }

	for (int amrlev = m_num_amr_levels-1; amrlev > 0; --amrlev)
	{
		averageDownCoeffsSameAmrLevel(amrlev);
		averageDownCoeffsToCoarseAmrLevel(amrlev);
	}

	averageDownCoeffsSameAmrLevel(0);

	for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
	{
		for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
		{
			if (model[amrlev][mglev]) {
				FillBoundaryCoeff(*model[amrlev][mglev], m_geom[amrlev][mglev]);
			}
		}
	}
}

template<class T>
void
Elastic<T>::averageDownCoeffsToCoarseAmrLevel (int flev) // this is where the problem is happening
{
	//Util::Message(INFO);
	const int mglev = 0;

	// const int idim = 0;  // other dimensions are just aliases

	// // amrex::average_down(*m_sigma[flev][mglev][idim], *m_sigma[flev-1][mglev][idim], 0, 1,
	// // 		    m_amr_ref_ratio[flev-1]);

	int ncomp = AMREX_SPACEDIM;
	MultiTab finemt;
	MultiTab crsemt;
	
	

	amrex::BoxArray fineBACoarsened = finemt.boxArray(); fineBACoarsened.coarsen(m_amr_ref_ratio[flev-1]);


	//MultiFab crse_S_fine(crse_S_fine_BA, S_fine.DistributionMap(), ncomp, 0, MFInfo(), FArrayBoxFactory());
        if ((fineBACoarsened == crsemt.boxArray()) &&
	    (finemt.DistributionMap() == crsemt.DistributionMap()))
	{
		Util::Message(INFO); // this never seems to happen
	}
	else
	{
		//Util::Abort(INFO, "difference in box arrays");

		MultiTab finemtcoarsened(fineBACoarsened, finemt.DistributionMap(), ncomp, 0);

// #ifdef _OPENMP
// #pragma omp parallel
// #endif
		for (MFIter mfi(finemtcoarsened,true); mfi.isValid(); ++mfi)
		{
			//  NOTE: The tilebox is defined at the coarse level.
			const Box& bx = mfi.tilebox();
                
			const TArrayBox &fine = finemtcoarsened[mfi];
			TArrayBox &crse = crsemt[mfi];

			//  NOTE: We copy from component scomp of the fine fab into component 0 of the crse fab
			//        because the crse fab is a temporary which was made starting at comp 0, it is
			//        not part of the actual crse multifab which came in.


			AMREX_D_TERM(for (int m1 = bx.loVect()[0]-1; m1<=bx.hiVect()[0]+1; m1++),
				     for (int m2 = bx.loVect()[1]-1; m2<=bx.hiVect()[1]+1; m2++),
				     for (int m3 = bx.loVect()[2]-1; m3<=bx.hiVect()[2]+1; m3++))
			{
				amrex::IntVect m_crse(AMREX_D_DECL(m1,m2,m3));
				amrex::IntVect m_fine(AMREX_D_DECL(m1*2,m2*2,m3*2));

				crse(m_crse) = fine(m_fine);
			}
		}
            
		crsemt.copy(finemtcoarsened,0,0,ncomp);

	}
}

template<class T>
void
Elastic<T>::averageDownCoeffsSameAmrLevel (int amrlev)
{
	BL_PROFILE("Elastic::averageDownCoeffsSameAmrLevel()");
	//Util::Message(INFO,"Appears to work.");

// 	if (m_coarsening_strategy != CoarseningStrategy::Sigma) return;

// 	const int nsigma = (m_use_harmonic_average) ? AMREX_SPACEDIM : 1;

 	for (int mglev = 1; mglev < m_num_mg_levels[amrlev]; ++mglev)
 	{
		MultiTab& crse = *model[amrlev][mglev];
		MultiTab& fine = *model[amrlev][mglev-1];
		
		bool isMFIterSafe  = (crse.DistributionMap() == fine.DistributionMap()) && BoxArray::SameRefs(crse.boxArray(),fine.boxArray());
		bool need_parallel_copy = !isMFIterSafe;

		MultiTab cfine;
		if (need_parallel_copy) {
			const BoxArray& ba = amrex::coarsen(fine.boxArray(), 2);
			cfine.define(ba, fine.DistributionMap(), 1, 1);
		}

		MultiTab* pcrse = (need_parallel_copy) ? &cfine : &crse;

		for (MFIter mfi(*pcrse, true); mfi.isValid(); ++mfi)
 			{
				if (AMREX_SPACEDIM > 2) Util::Abort("works in 2D only!");

				const Box& bx = mfi.tilebox();

				TArrayBox &crsetab = (*pcrse)[mfi];
				TArrayBox &finetab = fine[mfi];

				AMREX_D_TERM(for (int m1 = bx.loVect()[0]-1; m1<=bx.hiVect()[0]+1; m1++),
					     for (int m2 = bx.loVect()[1]-1; m2<=bx.hiVect()[1]+1; m2++),
					     for (int m3 = bx.loVect()[2]-1; m3<=bx.hiVect()[2]+1; m3++))
				{

					amrex::IntVect m_crse(AMREX_D_DECL(m1,m2,m3));
					amrex::IntVect m_fine(AMREX_D_DECL(m1*2,m2*2,m3*2));

					Set::Scalar total = 0.0;

					if (m1 == bx.loVect()[0] - 1) ++m_fine[0];
					if (m2 == bx.loVect()[1] - 1) ++m_fine[1];
					if (m1 == bx.hiVect()[0] + 1) --m_fine[0];
					if (m2 == bx.hiVect()[1] + 1) --m_fine[1];
					
					crsetab(m_crse) = finetab(m_fine)*4.0; 
					total += 4.0;


					if (m1 > bx.loVect()[0]-1 && m1 < bx.hiVect()[0]+1)
					{
						crsetab(m_crse) += finetab(m_fine-dx[0])*2.0 + finetab(m_fine+dx[0])*2.0;
						total += 4.0;
					}	
					if (m2 > bx.loVect()[1]-1 && m2 < bx.hiVect()[1]+1)
					{
						crsetab(m_crse) += finetab(m_fine-dx[1])*2.0 + finetab(m_fine+dx[1])*2.0;
						total += 4.0;
					}	
					if (m1 > bx.loVect()[0]-1 && m1 < bx.hiVect()[0]+1 &&
					    m2 > bx.loVect()[1]-1 && m2 < bx.hiVect()[1]+1 )
					{
						crsetab(m_crse) +=
							finetab(m_fine-dx[0]-dx[1]) + finetab(m_fine-dx[0]+dx[1]) +
							finetab(m_fine+dx[0]-dx[1]) + finetab(m_fine+dx[0]+dx[1]);
						total += 4.0;
					}	
					crsetab(m_crse) = crsetab(m_crse) / total;

				}
 			}

		if (need_parallel_copy) {
			crse.ParallelCopy(cfine);
		}
 	}
}

template<class T>
void
Elastic<T>::FillBoundaryCoeff (amrex::FabArray<amrex::BaseFab<T> >& sigma, const Geometry& geom)
{
	BL_PROFILE("Elastic::FillBoundaryCoeff()");
	Util::Message(INFO);

	sigma.FillBoundary(geom.periodicity());

	const Box& domain = geom.Domain();

#ifdef _OPENMP
#pragma omp parallel
#endif
	// for (MFIter mfi(sigma, MFItInfo().SetDynamic(true)); mfi.isValid(); ++mfi)
	// {
	// 	if (!domain.contains(mfi.fabbox()))
	// 	{
			

	// 	}
	// }
	Util::Warning(INFO, "FillBoundaryCoeff not fully implemented");
}



template class Elastic<Model::Solid::Elastic::Isotropic>;
template class Elastic<Model::Solid::Elastic::Cubic>;
template class Elastic<Model::Solid::Elastic::Degradable::Isotropic>;

}

